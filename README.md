[![Build Firmware](https://github.com/wencescarlos/Retroclock/actions/workflows/build.yml/badge.svg)](https://github.com/wencescarlos/Retroclock/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![GitHub Release](https://img.shields.io/github/v/release/wencescarlos/Retroclock?sort=semver)](https://github.com/wencescarlos/Retroclock/releases)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Code Quality](https://github.com/wencescarlos/Retroclock/actions/workflows/quality.yml/badge.svg)](https://github.com/wencescarlos/Retroclock/actions/workflows/quality.yml)

---

## Tabla de Contenidos

- [Características](#características-)
- [Documentación](#documentación-)
- [Hardware](#hardware-)
- [Instalación](#instalación-)
- [Configuración](#configuración-)
- [Dependencias](#dependencias-)
- [Uso](#uso-)
- [Estructura del Proyecto](#estructura-del-proyecto-)
- [Notas Técnicas](#notas-técnicas-)
- [Desarrollo](#desarrollo-)
- [Troubleshooting](#troubleshooting-)
- [Licencia](#licencia-)
- [Seguridad](#seguridad-)
- [Contribuciones](#contribuciones-)

---

## Retro Clock ⏰

Un reloj retro estilo **Tetris** para ESP32 con pantalla táctil (Cheap Yellow Display - CYD) que se sincroniza automáticamente por NTP.

## Características ✨

- **Reloj Tetris animado**: Muestra la hora (HH:MM) con bloques que caen al estilo Tetris
- **Piezas decorativas de fondo**: Animaciones Tetris que no invaden la zona de visualización del reloj
- **Sincronización NTP**: Actualización automática de la hora desde servidores NTP
- **Menu táctil**: Mantén presionado 2 segundos para acceder a las opciones:
  - 🌐 Configuración WiFi (portal AP con WiFiManager)
  - 🕐 Zona horaria (UTC-12 a UTC+12)
  - ☀️ Control de brillo de pantalla
  - 🔢 Formato de hora 12h / 24h
  - ⏰ Horario de pantalla (encendido/apagado automático)
- **Persistencia**: Todos los ajustes se guardan en la memoria flash (NVS/Preferences)
- **Múltiples modos de visualización**: Tetris, Analógico, Pac-Man, Conway, Retro, Fractal, Pixel, Solar

## Documentación 📚

- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Guía para desarrolladores y contribuidores
- **[SECURITY.md](SECURITY.md)** - Política de seguridad y cómo reportar vulnerabilidades
- **[CHANGELOG.md](CHANGELOG.md)** - Histórico de cambios y roadmap futuro
- **[LICENSE](LICENSE)** - Licencia MIT
- **GitHub Workflows**: Compilación y release automática (ver `.github/workflows/`)

## Hardware 🔧

**Placa**: ESP32-2432S028Rv3 (Cheap Yellow Display)

### Componentes principales:
- **Display**: ST7789 (240x320 px, SPI)
- **Touch**: XPT2046 (SPI independiente)
- **LED RGB**: Anodo común (GPIO 4, 16, 17)
- **Tarjeta SD**: Soporte opcional (GPIO 5, 23, 18, 19)

## Instalación 🚀

### Requisitos previos:
- [PlatformIO](https://platformio.org/)
- ESP32-2432S028Rv3 (o compatible)
- Conexión USB

### Pasos:

1. **Clona el repositorio**:
   ```bash
   git clone https://github.com/wencescarlos/Retroclock.git
   cd Retroclock
   ```

2. **Abre el proyecto en PlatformIO**:
   ```bash
   pio run --target upload -e CYD_RetroClock
   ```

3. **Monitor serie** (opcional, para debugging):
   ```bash
   pio device monitor -b 115200
   ```

## Configuración 📋

### WiFi y Zona horaria
1. Enciende el dispositivo
2. Mantén presionado la pantalla durante 2 segundos
3. Selecciona "Configurar WiFi"
4. Conéctate al AP del dispositivo
5. Ingresa tus credenciales WiFi
6. Ajusta la zona horaria en el menú

### Calibración del Touch (opcional)
Si el toque no coincide con la pantalla, descomenta en `main.cpp`:
```cpp
// Serial.printf("TOUCH raw x=%d y=%d z=%d\n", raw.x, raw.y, raw.z);
```
Luego ajusta en `main.cpp`:
```cpp
#define T_XMIN  200
#define T_XMAX  3900
#define T_YMIN  200
#define T_YMAX  3800
```

## Dependencias 📚

- **TFT_eSPI** (v2.5.43+): Librería para el display ST7789
- **NTPClient** (v3.2.1+): Sincronización de hora
- **WiFiManager** (v2.0.17+): Portal de configuración WiFi
- **XPT2046_Touchscreen**: Controlador del panel táctil

Todas las dependencias se instalan automáticamente con PlatformIO.

## Uso 💡

### Interfaz principal
- La pantalla muestra el reloj Tetris con la hora actual
- Piezas decorativas caen en el fondo (solo modo Tetris)

### Menú táctil
Mantén presionado 2 segundos en la pantalla para abrir el menú:
- **WiFi**: Configura/reconfigura conexión
- **Zona Horaria**: Cambia entre UTC-12 y UTC+12
- **Brillo**: Ajusta de 0-255
- **Formato 12h/24h**: Cambia formato de visualización
- **Horario**: Programa encendido/apagado automático

## Estructura del proyecto 📁

```
Retroclock/
├── .github/
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md          # Template para reportar bugs
│   │   └── feature_request.md     # Template para solicitar features
│   ├── pull_request_template.md   # Template para PRs
│   └── workflows/
│       ├── build.yml              # Compilación automática en cada push
│       ├── release.yml            # Release automática con tags
│       └── quality.yml            # Validación de código y linting
├── src/
│   ├── main.cpp                   # Lógica principal
│   └── board_config.h             # Configuración de pines
├── .editorconfig                  # Configuración del editor (indentación, etc)
├── .gitignore                     # Archivos ignorados por git
├── platformio.ini                 # Configuración de PlatformIO
├── README.md                      # Este archivo
├── CONTRIBUTING.md                # Guía para contribuidores
├── SECURITY.md                    # Política de seguridad
├── CHANGELOG.md                   # Histórico de cambios
├── LICENSE                        # Licencia MIT
└── .gitignore                     # Archivos ignorados
```

## Notas técnicas ⚙️

- **SPI**: El display usa VSPI (TFT_eSPI) y el touch usa HSPI para evitar conflictos
- **Velocidad SPI**: 40 MHz para el display (tolerancia típica del ST7789)
- **Frecuencia de muestreo del touch**: 20ms para suavidad
- **Presión mínima del touch**: 600 (umbral de detección)

## Desarrollo 🔧

### Setup para desarrolladores

```bash
# Clona y configura
git clone https://github.com/wencescarlos/Retroclock.git
cd Retroclock
git checkout -b develop

# Compila lokalmente
pio run -e CYD_RetroClock

# Flashea a la placa
pio run --target upload -e CYD_RetroClock

# Monitorea salida
pio device monitor -b 115200
```

### Validación previa al commit

Asegúrate que el código:
- ✅ Compila sin warnings
- ✅ Sigue las convenciones (`.editorconfig`)
- ✅ Pasa los tests de calidad
- ✅ Funciona en la placa

### Flujo de trabajo

Consulta [CONTRIBUTING.md](CONTRIBUTING.md) para:
- Convenciones de código
- Proceso de commits
- Cómo abrir PRs
- Reportar issues

### CI/CD Automático

Cuando haces push a `main` o `develop`:
1. 🏗️ **Build**: Compila el firmware
2. 🔍 **Quality**: Valida código y linting
3. 📦 **Artifacts**: Disponibles 30 días

Cuando creas un tag (`v1.0.0`):
1. 🏗️ **Build**: Compila el firmware
2. 📝 **Checksum**: Genera SHA256
3. 🚀 **Release**: Publica en GitHub Releases

## Troubleshooting 🐛

| Problema | Solución |
|----------|----------|
| Pantalla en blanco | Verifica los pines SPI en `board_config.h` |
| Touch no funciona | Calibra los valores T_XMIN/MAX, T_YMIN/MAX |
| WiFi no se conecta | Mantén presionado en la pantalla, reconfigura WiFi |
| Hora incorrecta | Verifica tu zona horaria en el menú |
| Parpadeos visuales | Reduce `SPI_FREQUENCY` a 27 MHz si es necesario |

## Licencia 📄

Este proyecto está bajo la licencia [MIT](LICENSE). Eres libre de usarlo, modificarlo y distribuirlo.

Para más detalles, consulta el archivo [LICENSE](LICENSE).

## Seguridad 🔒

Si descubres una vulnerabilidad de seguridad, **por favor no abras un issue público**. 

Consulta [SECURITY.md](SECURITY.md) para conocer cómo reportar vulnerabilidades de forma responsable.

## Autor y Contribudores

**Autor**: Wences

**Contribuidores**: ¡Tú! Consulta [CONTRIBUTING.md](CONTRIBUTING.md) para saber cómo contribuir.

## Contribuciones 🤝

Las contribuciones son bienvenidas y apreciadas. 

Para contribuir:
1. Lee [CONTRIBUTING.md](CONTRIBUTING.md)
2. Fork el proyecto
3. Crea una rama (`feature/mi-feature`)
4. Commit tus cambios
5. Abre un Pull Request

Aquí hay algunas áreas donde puedes ayudar:
- 🎨 Nuevos modos de visualización
- ⚡ Optimizaciones de rendimiento
- 📖 Mejora de documentación
- 🐛 Reportar y ayudar a fijar bugs
- 🌐 Traducción de documentación

## Recursos 📖

- [PlatformIO Docs](https://docs.platformio.org/)
- [ESP32 Documentation](https://docs.espressif.com/projects/esp32-rtos-sdk/)
- [TFT_eSPI GitHub](https://github.com/Bodmer/TFT_eSPI)
- [WiFiManager GitHub](https://github.com/tzapu/WiFiManager)

## Preguntas o Sugerencias?

- **Issues**: [Abre una issue](https://github.com/wencescarlos/Retroclock/issues)
- **Discussions**: [Participa en discusiones](https://github.com/wencescarlos/Retroclock/discussions)
- **Reportar bug**: Usa el [template de bug report](https://github.com/wencescarlos/Retroclock/issues/new?template=bug_report.md)
- **Sugerir feature**: Usa el [template de feature request](https://github.com/wencescarlos/Retroclock/issues/new?template=feature_request.md)

---

<div align="center">

Made with ❤️ para los amantes del Tetris retro

[⬆ Volver arriba](#retro-clock-)

</div>

