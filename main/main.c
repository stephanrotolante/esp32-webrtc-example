#include "main.h"

#include <esp_camera.h>
#include <esp_h264_alloc.h>
#include <esp_h264_enc_single.h>
#include <esp_h264_enc_single_sw.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_tls.h>
#include <freertos/FreeRTOS.h>
#include <nvs_flash.h>
#include <peer.h>
#include <string.h>
#include <sys/param.h>

#ifdef CONFIG_FREERTOS_UNICORE
#define CPU_NUM 1
#else
#define CPU_NUM CONFIG_SOC_CPU_CORES_NUM
#endif

long sysconf(int arg) {
  switch (arg) {
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
      return CPU_NUM;
    default:
      errno = EINVAL;
      return -1;
  }
}

void *my_esp_h264_aligned_calloc(uint32_t alignment, uint32_t n, uint32_t size,
                                 uint32_t *actual_size, uint32_t caps) {
  *actual_size = ALIGN_UP(n * size, alignment);
  void *out_ptr = heap_caps_aligned_calloc((size_t)alignment, (size_t)n,
                                           (size_t)size, caps);
  return out_ptr;
}

#define MAX_HTTP_OUTPUT_BUFFER 2048

char *g_latest_description = NULL;

PeerConnection *peer_connection;

uint8_t handshake = 0;

#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

int init_video_capture() {
  static camera_config_t camera_config = {.pin_pwdn = CAM_PIN_PWDN,
                                          .pin_reset = CAM_PIN_RESET,

                                          .pin_xclk = CAM_PIN_XCLK,

                                          .pin_sccb_sda = CAM_PIN_SIOD,
                                          .pin_sccb_scl = CAM_PIN_SIOC,

                                          .pin_d7 = CAM_PIN_D7,
                                          .pin_d6 = CAM_PIN_D6,
                                          .pin_d5 = CAM_PIN_D5,
                                          .pin_d4 = CAM_PIN_D4,
                                          .pin_d3 = CAM_PIN_D3,
                                          .pin_d2 = CAM_PIN_D2,
                                          .pin_d1 = CAM_PIN_D1,
                                          .pin_d0 = CAM_PIN_D0,

                                          .pin_vsync = CAM_PIN_VSYNC,
                                          .pin_href = CAM_PIN_HREF,

                                          .pin_pclk = CAM_PIN_PCLK,

                                          .xclk_freq_hz = 16000000,

                                          .ledc_timer = LEDC_TIMER_0,
                                          .ledc_channel = LEDC_CHANNEL_0,

                                          .pixel_format = PIXFORMAT_YUV422,
                                          .frame_size = FRAMESIZE_96X96,
                                          .jpeg_quality = 63,
                                          .fb_count = 10,
                                          .grab_mode = CAMERA_GRAB_WHEN_EMPTY};

  return esp_camera_init(&camera_config);
}

void on_connectionstatechange(PeerConnectionState state, void *user_data) {
  ESP_LOGI(LOG_TAG, "connection state: %s",
           peer_connection_state_to_string(state));
}

void on_icecandidate(char *description, void *user_data) {
  // Free the old description if it exists
  if (g_latest_description != NULL) {
    free(g_latest_description);
    g_latest_description = NULL;
  }

  // Allocate and copy the new description
  if (description != NULL) {
    g_latest_description = strdup(description);
    if (g_latest_description == NULL) {
      ESP_LOGE(LOG_TAG, "Failed to allocate memory for description");
    }

    handshake = 1;
  }
}

char *output_buffer;

int output_len;
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ERROR");
      break;
    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_CONNECTED");
      break;
    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_HEADER_SENT");
      break;
    case HTTP_EVENT_ON_HEADER:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
               evt->header_key, evt->header_value);
      break;
    case HTTP_EVENT_ON_DATA:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      // Clean the buffer in case of a new request
      if (output_len == 0 && evt->user_data) {
        // we are just starting to copy the output data into the use
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
      }
      /*
       *  Check for chunked encoding is added as the URL for chunked encoding
       * used in this example returns binary data. However, event handler can
       * also be used in case chunked encoding is used.
       */
      if (!esp_http_client_is_chunked_response(evt->client)) {
        // If user_data buffer is configured, copy the response into the buffer
        int copy_len = 0;
        if (evt->user_data) {
          // The last byte in evt->user_data is kept for the NULL character in
          // case of out-of-bound access.
          copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
          if (copy_len) {
            memcpy(evt->user_data + output_len, evt->data, copy_len);
          }
        } else {
          int content_len = esp_http_client_get_content_length(evt->client);
          if (output_buffer == NULL) {
            // We initialize output_buffer with 0 because it is used by strlen()
            // and similar functions therefore should be null terminated.
            output_buffer = (char *)calloc(content_len + 1, sizeof(char));
            output_len = 0;
            if (output_buffer == NULL) {
              ESP_LOGE(LOG_TAG, "Failed to allocate memory for output buffer");

              return ESP_FAIL;
            }
          }
          copy_len = MIN(evt->data_len, (content_len - output_len));
          if (copy_len) {
            memcpy(output_buffer + output_len, evt->data, copy_len);
          }
        }
        output_len += copy_len;
      } else {
        if (output_buffer == NULL) {
          output_buffer = (char *)calloc(evt->data_len, sizeof(char));
          memcpy(output_buffer, evt->data, evt->data_len);
        } else {
          char *temp_buffer =
              (char *)calloc(output_len + evt->data_len, sizeof(char));
          memcpy(temp_buffer, output_buffer, output_len);
          memcpy(temp_buffer + output_len, evt->data, evt->data_len);
          free(output_buffer);
          output_buffer = temp_buffer;
        }

        output_len += evt->data_len;
      }

      break;
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_FINISH");
      if (output_buffer != NULL) {
        handshake = 2;
      }
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_DISCONNECTED");
      int mbedtls_err = 0;
      esp_err_t err = esp_tls_get_and_clear_last_error(
          (esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
      if (err != 0) {
        ESP_LOGI(LOG_TAG, "Last esp error code: 0x%x", err);
        ESP_LOGI(LOG_TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
      }
      break;
    case HTTP_EVENT_REDIRECT:
      break;
  }
  return ESP_OK;
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  esp_h264_enc_handle_t h264_encoder = NULL;
  esp_h264_enc_cfg_sw_t cfg;
  cfg.gop = 1;
  cfg.fps = 15;
  cfg.res.width = 96;
  cfg.res.height = 96;
  cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
  cfg.rc.qp_min = 30;
  cfg.rc.qp_max = 30;
  cfg.pic_type = ESP_H264_RAW_FMT_YUYV;

  int e;

  if ((e = esp_h264_enc_sw_new(&cfg, &h264_encoder)) != ESP_H264_ERR_OK) {
    ESP_LOGE(LOG_TAG, "error occured creating h264 encoder: %d", e);
    return;
  }

  if ((e = esp_h264_enc_open(h264_encoder)) != ESP_H264_ERR_OK) {
    ESP_LOGE(LOG_TAG, "error occurred opening h264 encodeer: %d", e);
    return;
  }

  esp_h264_enc_in_frame_t in_frame;
  esp_h264_enc_out_frame_t out_frame;
  camera_fb_t *fb = NULL;

  if (init_video_capture() != 0) {
    ESP_LOGE(LOG_TAG, "init_video_capture() != 0");
    return;
  }

  // Block until connection
  wifi();

  peer_init();

  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_NONE,
      .video_codec = CODEC_H264,
      .datachannel = DATA_CHANNEL_NONE,
      .onaudiotrack = NULL,
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_connection = peer_connection_create(&peer_connection_config);

  if (peer_connection == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create peer connection");
    return;
  }

  peer_connection_oniceconnectionstatechange(peer_connection,
                                             on_connectionstatechange);
  peer_connection_onicecandidate(peer_connection, on_icecandidate);

  peer_connection_create_offer(peer_connection);

  while (handshake == 0) {
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  esp_http_client_config_t config = {
      .url = "http://192.168.1.201:8080/whip",
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
      .event_handler = http_event_handler,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  const char *_temp = g_latest_description;

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_post_field(client, _temp, strlen(_temp));
  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI(LOG_TAG, "HTTP POST Status = %d, content_length = %" PRId64,
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(LOG_TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }

  while (handshake == 1) {
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  esp_http_client_cleanup(client);

  const char *_output_buffer = output_buffer;
  peer_connection_set_remote_description(peer_connection, _output_buffer);

  while (1) {
    peer_connection_loop(peer_connection);

    fb = esp_camera_fb_get();

    if (!fb) {
      ESP_LOGE(LOG_TAG, "camera capture failed");
      esp_camera_fb_return(fb);
      continue;
    }

    in_frame.raw_data.len = fb->len;
    in_frame.raw_data.buffer = fb->buf;

    out_frame.raw_data.len = fb->width * fb->height * 2;
    out_frame.raw_data.buffer = (uint8_t *)my_esp_h264_aligned_calloc(
        16, 1, out_frame.raw_data.len, &out_frame.raw_data.len,
        MALLOC_CAP_SPIRAM);

    if ((e = esp_h264_enc_process(h264_encoder, &in_frame, &out_frame)) !=
        ESP_H264_ERR_OK) {
      ESP_LOGE(LOG_TAG, "failed to encode %d", e);
      heap_caps_free(out_frame.raw_data.buffer);
      esp_camera_fb_return(fb);
      continue;
    }

    peer_connection_send_video(peer_connection, out_frame.raw_data.buffer,
                               out_frame.length);

    heap_caps_free(out_frame.raw_data.buffer);

    vTaskDelay(pdMS_TO_TICKS(20));
    esp_camera_fb_return(fb);
  }
}