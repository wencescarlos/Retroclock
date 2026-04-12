# Changelog

Todos los cambios importantes de este proyecto se documentan en este archivo.

El formato se basa en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/),
y este proyecto adhiere a [Semantic Versioning](https://semver.org/lang/es/).

## [Unreleased]
### Added
- Soporte para múltiples modos de reloj (Tetris, Analógico, Pac-Man, Conway, Retro, Fractal, Pixel, Solar)
- Menú táctil con configuración de WiFi, zona horaria, brillo y formato de hora
- Sincronización automática de hora via NTP
- Guardar configuración en memoria flash (NVS)
- Horario programable de encendido/apagado de pantalla

### Changed
- Mejora en la calibración del touch

### Fixed
- Consulta los PRs abiertos

### Security
- Credenciales WiFi se guardan de forma segura en NVS

## [Pending Release] - 2026-04-12
### Added
- README.md con documentación completa
- CONTRIBUTING.md con guía para desarrolladores
- Templates de Issues (bug_report, feature_request)
- Template de Pull Requests
- Workflows de GitHub Actions (build, release, quality)
- LICENSE (MIT)
- SECURITY.md
- CHANGELOG.md
- .editorconfig

### Changed
- Estructura base del proyecto finalizada

---

## [1.0.0] - 2026-04-XX (Próxima Release)
### Added
- ✅ Reloj Tetris animado con sincronización NTP
- ✅ Panel táctil responsivo (XPT2046)
- ✅ WiFi con portal de configuración (WiFiManager)
- ✅ Zona horaria configurable (UTC-12 a UTC+12)
- ✅ Control de brillo de pantalla
- ✅ Formato 12h / 24h
- ✅ Horario programable (encendido/apagado automático)
- ✅ Support para LED RGB
- ✅ Persistencia de configuración en flash

### Hardware
- Placa: ESP32-2432S028Rv3 (Cheap Yellow Display)
- Display: ST7789 (240x320)
- Touch: XPT2046
- LED RGB: GPIO 4, 16, 17

---

## Notas sobre versionado

- **MAJOR** (1.0.0): Breaking changes, cambio de plataforma soportada
- **MINOR** (1.1.0): Nuevas features compatibles hacia atrás
- **PATCH** (1.0.1): Bugfixes y mejoras sin cambiar API

## Tags

Todas las versiones se tagean en GitHub:
```bash
git tag v1.0.0
git push origin v1.0.0
```

El workflow de release se ejecutará automáticamente.

## Roadmap

### v1.1.0 (Próximo)
- [ ] Más modos de visualización
- [ ] Soporte para SD Card (reproducción de imágenes)
- [ ] Interfaz mejorada
- [ ] Más temas de colores

### v1.2.0
- [ ] API HTTP para controladores externos
- [ ] MQTT support
- [ ] Sincronización de hora con otros dispositivos
- [ ] Grabación de datos (temperatura, humedad si se añaden sensores)

### v2.0.0 (Futuro)
- [ ] Soporte para múltiples placas ESP32
- [ ] Sistema de plugins
- [ ] Aplicación móvil de control

---

## Cómo reportar cambios

Al crear un commit, usa los prefijos:
- `feat:` Nueva funcionalidad
- `fix:` Corrección de bug
- `docs:` Cambios de documentación
- `refactor:` Mejora de código
- `perf:` Optimización
- `test:` Pruebas

Ejemplo:
```
feat: add analog clock mode
fix: correct touch calibration for landscape
```

Cuando hagas un PR que cierra un issue:
```
Closes #123
```

Los maintainers actualizarán este CHANGELOG según las releases oficiales.

---

**Última actualización**: 12 de abril de 2026
