#include <WiFi.h>
#include <esp32-hal-ledc.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"

const char* ssid = "";
const char* password = "";

const char* apssid = "ESP32-Ron";
const char* appassword = "12345678";

String Feedback="";
String Command="";
String cmd="";
byte ReceiveState=0;
byte cmdState=1;
byte strState=1;
byte questionState=0;
byte equalState=0;

String P1="";
String P2="";
String P3="";
String P4="";
String P5="";
String P6="";
String P7="";


typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY= "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART ="Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27  
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void initCamera()
{
 camera_config_t config;
 config.ledc_channel = LEDC_CHANNEL_0;
 config.ledc_timer = LEDC_TIMER_0;
 config.pin_d0 = Y2_GPIO_NUM;
 config.pin_d1 = Y3_GPIO_NUM;
 config.pin_d2 = Y4_GPIO_NUM;
 config.pin_d3 = Y5_GPIO_NUM;
 config.pin_d4 = Y6_GPIO_NUM;
 config.pin_d5 = Y7_GPIO_NUM;
 config.pin_d6 = Y8_GPIO_NUM;
 config.pin_d7 = Y9_GPIO_NUM;
 config.pin_xclk = XCLK_GPIO_NUM;
 config.pin_pclk = PCLK_GPIO_NUM;
 config.pin_vsync = VSYNC_GPIO_NUM;
 config.pin_href = HREF_GPIO_NUM;
 config.pin_sscb_sda = SIOD_GPIO_NUM;
 config.pin_sscb_scl = SIOC_GPIO_NUM;
 config.pin_pwdn = PWDN_GPIO_NUM;
 config.pin_reset = RESET_GPIO_NUM;
 config.xclk_freq_hz = 20000000;
 config.pixel_format = PIXFORMAT_JPEG; 
  
 if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
 } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
 }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

static size_t jpg_encode_stream(void *arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len=0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;

    fb=esp_camera_fb_get();
    if(!fb){
        Serial.println("Camera capture failed!");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content_Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t fb_len = 0;
    if(fb->format == PIXFORMAT_JPEG){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *) fb->buf,fb->len);
    } else {
        jpg_chunking_t jchunk = {req, 0};
        res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);
        fb_len=jchunk.len;
    }
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t stream_handler(httpd_req *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res=httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res !=ESP_OK){
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin","*");
    while(true){
        fb = esp_camera_fb_get();
        if(!fb) {
            Serial.println("Camera capture failed!");
            res = ESP_FAIL; 
        } else {
            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if(!jpeg_converted){
                    Serial.println("JPEG compression failed");
                    res =ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf= fb->buf;
            }
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res =httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }

        if(fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf){
            free(_jpg_buf);
            _jpg_buf=NULL;
        }

        if(res != ESP_OK){
            break;
        }
    }
    return res;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];
    sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++= '{';
    p+=sprintf(p,"\"flash\":%d,", 0);
    p+=sprintf(p,"\"framesize\":%u,", s->status.framesize);
    p+=sprintf(p,"\"quality\":%u,", s->status.quality);
    p+=sprintf(p,"\"brightness\":%d,", s->status.brightness);
    p+=sprintf(p,"\"contrast\":%d,", s->status.contrast);
    p+=sprintf(p,"\"hmirror\":%u,", s->status.hmirror);
    p+=sprintf(p,"\"vflip\":%u",s->status.vflip);
    *p++ ='}';
    *p++ =0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

void getCommand(char c)
{
    if(c=='?') ReceiveState=1;
    if((c==' ')||(c=='\r')||(c=='\n')) ReceiveState=0;

    if (ReceiveState==1){
        Command=Command+String(c);
        if(c=='=') cmdState=0;
        if(c==';') strState++;
        if((cmdState==1)&&((c!='?')||(questionState==1))) cmd=cmd+String(c);
        if((cmdState==0)&&(strState==1) &&((c!='=')||(equalState==1))) P1=P1+String(c);
        if((cmdState==0)&&(strState==2) &&(c!=';')) P2=P2+String(c);
        if((cmdState==0)&&(strState==2) &&(c!=';')) P3=P3+String(c);
        if((cmdState==0)&&(strState==2) &&(c!=';')) P4=P4+String(c);
        if((cmdState==0)&&(strState==2) &&(c!=';')) P5=P5+String(c);
        if((cmdState==0)&&(strState==2) &&(c!=';')) P6=P6+String(c);
        if((cmdState==0)&&(strState==2) &&(c!=';')) P7=P7+String(c);
        
    }
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char variable[128]={0,};
    char value[128]={0,};
    String myCmd = "";

    buf_len = httpd_req_get_url_query_len(req)+1;
    if(buf_len >1){
        buf=(char*)malloc(buf_len);
        if(!buf){
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if(httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK){
            if(httpd_query_key_value(buf,"var",variable,sizeof(variable)) == ESP_OK && 
               httpd_query_key_value(buf,"val", value, sizeof(value))==ESP_OK){}
            else{
               myCmd=String(buf); 
            }
        }
        free(buf);
    }
    else{
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    Feedback="";Command="";cmd="";
    if(myCmd.length()>0){
        myCmd="?"+myCmd;
        for(int i=0;i<myCmd.length();i++)
        {
            getCommand(char(myCmd.charAt(i)));
        }
        if (Feedback=="") Feedback=Command;
        const char *resp = Feedback.c_str();
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Access-Control-Allow_Origin", "*");
        return httpd_resp_send(req, resp, strlen(resp));  
    }
    else{
        int val=atoi(value);
        sensor_t *s = esp_camera_sensor_get();
        int res=0;

        if(!strcmp(variable, "framesize")) {
           if(s->pixformat == PIXFORMAT_JPEG)
                res= s->set_framesize(s, (framesize_t)val); 
        }
        else if(!strcmp(variable, "quality")) res=s->set_quality(s,val);
        else if(!strcmp(variable, "brightness")) res=s->set_brightness(s,val);
        else if(!strcmp(variable, "contrast")) res=s->set_contrast(s,val);
        else if(!strcmp(variable, "hmirror")) res=s->set_hmirror(s,val);
        else if(!strcmp(variable, "vflip")) res=s->set_vflip(s,val);
        else if(!strcmp(variable, "flash")) {
            ledcAttachPin(4,4);
            ledcSetup(4,5000,8);
            ledcWrite(4, val);
        }
        else {
            res = -1;
        }
        if(res){
            return httpd_resp_send_500(req);
        }
        if(buf){
            Feedback=String(buf);
            const char *resp = Feedback.c_str();
            httpd_resp_set_type(req, "text/html");
            httpd_resp_set_hdr(req, "Access-Control-Allow_Origin", "*");
            return httpd_resp_send(req, resp, strlen(resp));          
        }
        else{
            httpd_resp_set_hdr(req, "Access-Control-Allow_Origin", "*");
            return httpd_resp_send(req, NULL, 0);
        }
    }
}

static const char PROGMEM INDEX_HTML[] = R"rawliteral(<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="Access-Control-Allow-Headers" content="Origin, X_Requested-With, Content-Type, Accept">
    <title>Camera 32 & Teachable Machine</title>
    <script src="https:\/\/ajax.googleapis.com/ajax/libs/jquery/1.8.0/jquery.min.js"></script>
    <script src="https:\/\/cdn.jsdelivr.net/npm/@tensorflow/tfjs@latest/dist/tf.min.js"></script>
    <script src="https:\/\/cdn.jsdelivr.net/npm/@teachablemachine/image@latest/dist/teachablemachine-image.min.js"></script>
    <style>
        body { font-family: Arial, Helvetica, sans-serif; background: #181818; color: #efefef; font-size: 16px; } h2 { font-size: 18px; } section.main { display: flex; } #menu, section.main { flex-direction: column; } #menu { display: flex; flex-wrap: nowrap; min-width: 340px; background: #363636; padding: 8px; border-radius: 4px; margin-top: -10px; margin-right: 10px; } #content { display: flex; flex-wrap: wrap; align-items: stretch; } figure { padding: 0; margin: 0; -webkit-margin-before: 0; margin-block-start: 0; -webkit-margin-after: 0; margin-block-end: 0; -webkit-margin-start: 0; margin-inline-start: 0; -webkit-margin-end: 0; margin-inline-end: 0; } figure img { display: block; width: 100%; height: auto; border-radius: 4px; margin-top: 8px; } @media (min-width: 800px) and (orientation: landscape) { #content { display: flex; flex-wrap: nowrap; align-items: stretch; } figure img { display: block; max-width: 100%; max-height: calc(100vh - 40px); width: auto; height: auto; } figure { padding: 0; margin: 0; -webkit-margin-before: 0; margin-block-start: 0; -webkit-margin-after: 0; margin-block-end: 0; -webkit-margin-start: 0; margin-inline-start: 0; -webkit-margin-end: 0; margin-inline-end: 0; } } section#buttons { display: flex; flex-wrap: nowrap; justify-content: space-between; } #nav-toggle { cursor: pointer; display: block; } #nav-toggle-cb { outline: 0; opacity: 0; width: 0; height: 0; } #nav-toggle-cb:checked + #menu { display: none; } .input-group { display: flex; flex-wrap: nowrap; line-height: 22px; margin: 5px 0; } .input-group > label { display: inline-block; padding-right: 10px; min-width: 47%; } .input-group input, .input-group select { flex-grow: 1; } .range-max, .range-min { display: inline-block; padding: 0 5px; } button { display: block; margin: 5px; padding: 0 12px; border: 0; line-height: 28px; cursor: pointer; color: #fff; background: #ff3034; border-radius: 5px; font-size: 16px; outline: 0; } button:hover { background: #ff494d; } button:active { background: #f21c21; } button.disabled { cursor: default; background: #a0a0a0; } input[type="range"] { -webkit-appearance: none; width: 100%; height: 22px; background: #363636; cursor: pointer; margin: 0; } input[type="range"]:focus { outline: 0; } input[type="range"]::-webkit-slider-runnable-track { width: 100%; height: 2px; cursor: pointer; background: #efefef; border-radius: 0; border: 0 solid #efefef; } input[type="range"]::-webkit-slider-thumb { border: 1px solid rgba(0, 0, 30, 0); height: 22px; width: 22px; border-radius: 50px; background: #ff3034; cursor: pointer; -webkit-appearance: none; margin-top: -11.5px; } input[type="range"]:focus::-webkit-slider-runnable-track { background: #efefef; } input[type="range"]::-moz-range-track { width: 100%; height: 2px; cursor: pointer; background: #efefef; border-radius: 0; border: 0 solid #efefef; } input[type="range"]::-moz-range-thumb { border: 1px solid rgba(0, 0, 30, 0); height: 22px; width: 22px; border-radius: 50px; background: #ff3034; cursor: pointer; } input[type="range"]::-ms-track { width: 100%; height: 2px; cursor: pointer; background: 0 0; border-color: transparent; color: transparent; } input[type="range"]::-ms-fill-lower { background: #efefef; border: 0 solid #efefef; border-radius: 0; } input[type="range"]::-ms-fill-upper { background: #efefef; border: 0 solid #efefef; border-radius: 0; } input[type="range"]::-ms-thumb { border: 1px solid rgba(0, 0, 30, 0); height: 22px; width: 22px; border-radius: 50px; background: #ff3034; cursor: pointer; height: 2px; } input[type="range"]:focus::-ms-fill-lower { background: #efefef; } input[type="range"]:focus::-ms-fill-upper { background: #363636; } .switch { display: block; position: relative; line-height: 22px; font-size: 16px; height: 22px; } .switch input { outline: 0; opacity: 0; width: 0; height: 0; } .slider { width: 50px; height: 22px; border-radius: 22px; cursor: pointer; background-color: grey; } .slider, .slider:before { display: inline-block; transition: 0.4s; } .slider:before { position: relative; content: ""; border-radius: 50%; height: 16px; width: 16px; left: 4px; top: 3px; background-color: #fff; } input:checked + .slider { background-color: #ff3034; } input:checked + .slider:before { -webkit-transform: translateX(26px); transform: translateX(26px); } select { border: 1px solid #363636; font-size: 14px; height: 22px; outline: 0; border-radius: 5px; } .image-container { position: relative; min-width: 160px; } .close { position: absolute; right: 5px; top: 5px; background: #ff3034; width: 16px; height: 16px; border-radius: 100px; color: #fff; text-align: center; line-height: 18px; cursor: pointer; } .hidden { display: none; }
    </style>
</head> 
<body>
  <section class="main">
    <figure>
      <div id="stream-container" class="image-container hidden">
        <div class="close" id="close-stream" style="display:none">x</div>
        <img id="stream" src="" style="display:none" crossorigin="anonymous">
        <canvas id="canvas" width="0" , hegiht="0"></canvas>
      </div>
    </figure>
    <section id="buttons">
      <table>
        <tr>
          <td><button id="restart"
              onclick="try{fetch(document.location.origin+'/control?restart');}catch(e){}">Restart</button></td>
          <td><button id="get-still" style="display:none">Get Still</button></td>
          <td><button id="toggle-stream" style="display:none"></button></td>
        </tr>
      </table>
    </section>
    <div id="logo">
      <label for="nav-toggle-cb" id="nav-toggle">Toggle Settings</label>
    </div>
    <div id="content">
      <div id="sidebar">
        <input type="checkbox" id="nav-toggle-cb">
        <nav id="menu">
          <div class="input-group">
            <label for="kind">Kind</label>
            <select id="kind">
              <option value="image">image</option>
            </select>
          </div>
          <div class="input-group">
            <label for="modelPath">Model Path</label>
            <input type="text" id="modelPath" value="">
          </div>
          <div class="input-group">
            <label for="btnModel"></label>
            <button type="button" id="btnModel" onclick="LoadModel();">Start Recognition
          </div>
          <div class="input-group" id="flash-group">
            <label for="flash">Flash</label>
            <div class="range-min">0</div>
            <input type="range" id="flash" min="0" max="255" value="0" class="default-action">
            <div class="range-max">255</div>
          </div>
          <div class="input-group" id="framesize-group">
            <label for="framesize">Resolution</label>
            <select id="framesize" class="default-action">
              <option value="10">UXGA(1600*1200)</option>
              <option value="9">SXGA(1280*1024)</option>
              <option value="8">XGA(1024*768)</option>
              <option value="7">SVGA(800*600)</option>
              <option value="6">VGA(640*480)</option>
              <option value="5" selected="selected">CIF(400*296)</option>
              <option value="4">QVGA(320*240)</option>
              <option value="3">HQVGA(240*176)</option>
              <option value="0">QQVGA(160*120)</option>
            </select>
          </div>
          <div class="input-group" id="quality-group">
            <label for="quality">Quality</label>
            <div class="range-min">10</div>
            <input type="range" id="quality" min="10" max="63" value="10" class="default-action">
            <div class="range-max">63</div>
          </div>

          <div class="input-group" id="brightness-group">
            <label for="brightness">Brightness</label>
            <div class="range-min">-2</div>
            <input type="range" id="brightness" min="-2" max="2" value="0" class="default-action">
            <div class="range-max">2</div>
          </div>

          <div class="input-group" id="contrast-group">
            <label for="contrast">Contrast</label>
            <div class="range-min">-2</div>
            <input type="range" id="contrast" min="-2" max="2" value="0" class="default-action">
            <div class="range-max">2</div>
          </div>

          <div class="input-group" id="hmirror-group">
            <label for="hmirror">HMirror</label>
            <div class="switch">
              <input type="checkbox" id="hmirror" class="default-action" checked="checked">
              <label class="slider" for="hmirror"></label>
            </div>
          </div>

          <div class="input-group" id="vflip-group">
            <label for="vflip">VFlip</label>
            <div class="switch">
              <input type="checkbox" id="vflip" class="default-action" checked="checked">
              <label class="slider" for="vflip"></label>
            </div>
          </div>
        </nav>
      </div>
    </div>
  </section>
  <div id="result" style="color:red"></div>

  <script>
    document.addEventListener("DOMContentLoaded", function (event) {
      var baseHost = document.location.origin;
      var streamUrl = baseHost + ":81";

      // 宣告函數        
      const hide = el => {
        el.classList.add("hidden");
      };
      const show = el => {
        el.classList.remove("hidden");
      };
      const disable = el => {
        el.classList.add("disabled");
        el.disabled = true;
      };
      const enable = el => {
        el.classList.remove("disabled");
        el.disabled = false;
      };
      const updateValue = (el, value, updateRemote) => {
        updateRemote = updateRemote == null ? true : updateRemote;
        let initialValue;
        if (el.type === "checkbox") {
          initalValue = el.checked;
          value = !!value;
          el.checked = value;
        } else {
          intialValue = el.value;
          el.value = value;
        }
        if (updateRemote && intialValue !== value) {
          updateConfig(el);
        }
      };

      function updateConfig(el) {
        let value;
        switch (el.type) {
          case "checkbox":
            value = el.checked ? 1 : 0;
            break;
          case "range":
          case "submit":
            value = "1";
            break;
          case "button":
          default:
            return;
        }

        const query = `${baseHost}/control?var=${el.id}&val=${value}`;
        fetch(query).then((response) => {
          console.log(`request to ${query} finised, status: ${response.status}`)
        });
      }

      document.querySelectorAll(".close").forEach(el => {
        el.onclick = () => {
          hide(el.parentNode);
        };
      });

      fetch(`${baseHost}/status`).then(function (response) {
        return response.json();
      }).then(function (state) {
        document.querySelectorAll(".default-action").forEach(el => {
          updateValue(el, state[el.id], false);
        });
      });

      const view = document.getElementById("stream");
      const viewContainer = document.getElementById("stream-container");
      const stillButton = document.getElementById("get-still");
      const streamButton = document.getElementById("toggle-stream");
      const closeButton = document.getElementById("close-stream");

      const stopStream = () => {
        view.src = "";
        streamButton.innerHTML = "Start Stream";
      }

      const startStream = () => {
        view.src = `${streamUrl}/stream`;
        show(viewContainer);
        streamButton.innerHTML = "Stop Stream";
      }

      stillButton.onclick = () => {
        stopStream();
        try {
          view.src = `${baseHost}/capture?_cb=${Date.now()}`;
        } catch (e) {
          view.src = `${baseHost}/capture?_cb=${Date.now()}`;
        }
        show(viewContainer);
      };

      closeButton.onclick = () => {
        stopStream();
        hide(viewContainer);
      };

      streamButton.onclick = () => {
        const streamEnabled = streamButton.innerHTML === "Stop Stream";
        if (streamEnabled) {
          stopStream();
        }
        else {
          startStream();
        }
      };

      document.querySelectorAll(".default-action").forEach(el => {
        el.onchange = () => updateConfig(el);
      });
    });

    var getStill = document.getElementById("get-still");
    var ShowImage = document.getElementById("stream");
    var canvas = document.getElementById("canvas");
    var context = canvas.getContext("2d");
    var modelPath = document.getElementById("modelPath");
    var result = document.getElementById("result");
    var kind = document.getElementById("kind");
    let Model;

    async function LoadModel() {
      if (modelPath.value == "") {
        result.innerHTML = "Please input model path.";
        return;
      }

      result.innerHTML = "Please wait for loading model.";
      const URL = modelPath.value;
      const metadataURL = URL + "metadata.json";
      const modelURL = URL + "model.json";
      Model = await tmImage.load(modelURL, metadataURL);

      maxPredictions = Model.getTotalClasses();
      result.innerHTML = "";
      getStill.style.display = "block";
      getStill.click();
    }

    async function predict() {
      var data = "";
      var maxClassName = "";
      var maxProbability = "";
      canvas.setAttribute("width", ShowImage.width);
      canvas.setAttribute("height", ShowImage.height);
      context.drawImage(ShowImage, 0, 0, ShowImage.width, ShowImage.height);

      if (kind.value == "image") {
        var prediction = await Model.predict(canvas);

        if (maxPredictions > 0) {
          for (let i = 0; i < maxPredictions; i++) {
            if (i == 0) {
              maxClassName = prediction[i].className;
              maxProbability = prediction[i].probability;
            } else {
              if (prediction[i].probability > maxProbability) {
                maxClassName = prediction[i].className;
                maxProbability = prediction[i].probability;
              }
            }
            data += prediction[i].className + "," + prediction[i].probability.toFixed(2) + "<br>";
            result.innerHTML = data;
            result.innerHTML += "<br>Result:" + maxClassName + "," + maxProbability;
          }
        } else result.innerHTML = "Unrecognized";
        getStill.click();
      }
    }

    ShowImage.onload = function (event) {
      if (Model) {
        try {
          document.createEvent("TouchEvent");
          setTimeout(function () {
            predict();
          }, 250);
        } catch (e) {
          predict();
        }
      }
    }
  </script>
</body>
</html>)rawliteral";

static esp_err_t index_handler(httpd_req *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char*)INDEX_HTML, strlen(INDEX_HTML));
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri ={
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t status_uri ={
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };

    httpd_uri_t cmd_uri ={
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL
    };

    httpd_uri_t capture_uri ={
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stream_uri ={
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config)==ESP_OK){
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port +=1;
    config.ctrl_port +=1;
    Serial.printf("Starting stream server on port : '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config)==ESP_OK){
        httpd_register_uri_handler(camera_httpd, &stream_uri);
    }

}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    Serial.println();
    initCamera();

    ledcAttachPin(4, 4);
    ledcSetup(4,5000,8);

    WiFi.mode(WIFI_AP_STA);

    for (int i=0;i<2;i++){
        WiFi.begin(ssid,password);
        delay(1000);
        Serial.println("");
        Serial.print("Connecting to ");
        Serial.println(ssid);

        long int StartTime=millis();
        while (WiFi.status() != WL_CONNECTED){
            delay(500);
            if((StartTime+5000) < millis()) break;
        }

        if(WiFi.status() == WL_CONNECTED){
            WiFi.softAP(WiFi.localIP().toString()+"_"+String(apssid),appassword);
            Serial.println("");
            Serial.println("STAIP address: ");
            Serial.println(WiFi.localIP());
            Serial.println("");
        }
    }

    Serial.println("");
    Serial.println("APIP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("");

    startCameraServer();

}

void loop()
{
    
}
