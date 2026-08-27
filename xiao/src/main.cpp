#include <Arduino.h>
#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"

// ---------- Pinout de cámara OV2640 en la XIAO ESP32S3 Sense ----------
// Estos son los pines fijos de fábrica del módulo Sense; no se remapean.
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// Intervalo entre fotos. Ponlo en 0 para ir lo más rápido posible
// (el límite real lo pone la cámara/SD, no este valor).
static const uint32_t INTERVALO_MS = 200;

static int contadorFoto = 0;

bool inicializarCamara() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
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

  // Si hay PSRAM (la Sense la trae), se puede pedir mayor resolución y calidad
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;   // 1600x1200
    config.jpeg_quality = 10;             // menor número = mejor calidad
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error al iniciar la camara: 0x%x\n", err);
    return false;
  }
  return true;
}

// Recorre la SD y busca el primer número de archivo libre, así al reiniciar
// la placa no se sobreescriben las fotos que ya estaban guardadas.
void recuperarContadorFoto() {
  char nombre[32];
  do {
    snprintf(nombre, sizeof(nombre), "/foto_%03d.jpg", contadorFoto);
    if (!SD_MMC.exists(nombre)) break;
    contadorFoto++;
  } while (true);
  Serial.printf("Reanudando numeracion desde %s\n", nombre);
}

bool inicializarSD() {
  // En la XIAO ESP32S3 Sense, la microSD comparte pines con la cámara,
  // por eso se usa el modo de 1 bit (bus más angosto) para evitar conflicto.
  if (!SD_MMC.setPins(39, 40, 26)) { // CLK, CMD, D0 (ver silkscreen/datasheet)
    Serial.println("Fallo al asignar pines de SD_MMC");
    return false;
  }

  if (!SD_MMC.begin("/sdcard", true)) { // true = modo 1-bit
    Serial.println("No se pudo montar la microSD");
    return false;
  }

  uint8_t tipoTarjeta = SD_MMC.cardType();
  if (tipoTarjeta == CARD_NONE) {
    Serial.println("No se detecto tarjeta microSD");
    return false;
  }

  uint64_t totalMB = SD_MMC.totalBytes() / (1024 * 1024);
  uint64_t usadoMB = SD_MMC.usedBytes() / (1024 * 1024);
  Serial.printf("SD: %llu MB usados de %llu MB\n", usadoMB, totalMB);

  return true;
}

void tomarYGuardarFoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Fallo al capturar la imagen");
    return;
  }

  char nombreArchivo[32];
  snprintf(nombreArchivo, sizeof(nombreArchivo), "/foto_%03d.jpg", contadorFoto++);

  File archivo = SD_MMC.open(nombreArchivo, FILE_WRITE);
  if (!archivo) {
    Serial.println("No se pudo crear el archivo en la SD");
    esp_camera_fb_return(fb);
    return;
  }

  archivo.write(fb->buf, fb->len);
  archivo.close();

  Serial.printf("Foto guardada: %s (%u bytes)\n", nombreArchivo, fb->len);

  esp_camera_fb_return(fb); // libera el buffer de PSRAM
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!inicializarCamara()) {
    Serial.println("Deteniendo: fallo de camara");
    while (true) delay(1000);
  }

  if (!inicializarSD()) {
    Serial.println("Deteniendo: fallo de SD");
    while (true) delay(1000);
  }

  recuperarContadorFoto();

  Serial.println("Camara y SD listas. Iniciando captura continua...");
}

void loop() {
  tomarYGuardarFoto();

  // Cada 50 fotos, avisa si queda poco espacio (menos de 50 MB libres)
  if (contadorFoto % 50 == 0) {
    uint64_t libreMB = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
    if (libreMB < 50) {
      Serial.printf("ADVERTENCIA: solo quedan %llu MB libres en la SD\n", libreMB);
    }
  }

  if (INTERVALO_MS > 0) {
    delay(INTERVALO_MS);
  }
}