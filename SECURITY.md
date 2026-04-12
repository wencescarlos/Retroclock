# Política de Seguridad

## Reportar una vulnerabilidad

Si descubres una vulnerabilidad de seguridad en Retro Clock, por favor **NO abras un issue público**. Las vulnerabilidades de seguridad deben reportarse de forma privada.

### Cómo reportar

**Opción 1: GitHub Security Advisory (Recomendado)**
1. Ve a [Security → Report a vulnerability](https://github.com/wencescarlos/Retroclock/security/advisories/new)
2. Completa el formulario con:
   - Descripción detallada de la vulnerabilidad
   - Pasos para reproducir
   - Impacto potencial
   - Versiones afectadas

**Opción 2: Email privado**
Marca un email con asunto `[SECURITY] Retro Clock Vulnerability` y envía a:
```
tu_email@example.com
```

### Información a incluir

- Descripción clara de la vulnerabilidad
- Pasos detallados para reproducir
- Impacto potencial (ej: código execution, acceso a datos privados, DoS)
- Versiones/commits afectados
- Cualquier parche o workaround que conozcas
- Tu nombre y preferencia de atribución

## Respuesta y Parche

- **Reconocimiento**: Responderemos dentro de 48 horas
- **Evaluación**: Valoraremos la severidad (Crítica, Alta, Media, Baja)
- **Coordinar**: Trabajaremos contigo en un cronograma razonable
- **Parche**: Publicaremos un patch dentro de:
  - **Crítica**: 24-72 horas
  - **Alta**: 1 semana
  - **Media**: 2-4 semanas
  - **Baja**: En la siguiente release o PR

## Severidad

### Crítica
- Ejecución remota de código (RCE)
- Acceso no autorizado a memoria
- Wipeout de memoria flash / datos
- Consecuencias de seguridad física

### Alta
- Inyección de comandos
- Acceso a credenciales WiFi
- Modificación de la hora del sistema
- Consumo de recursos DoS

### Media
- Fallos de compilación en ciertos ambientes
- Información disclosure limitada
- Bugs lógicos que afecten funcionalidad crítica

### Baja
- Bugs visuales menores
- Problemas de documentación
- Issues que requieren acceso físico a la placa

## Divulgación

Una vez que el parche esté disponible:
1. Publicamos una release con el fix
2. Actualizamos el CHANGELOG
3. Se te acredita en el release (a menos que prefieras anonimato)
4. Notificamos públicamente sobre la vulnerabilidad (sin detalles de explot hasta que todos hayan actualizado)

## Ecosistema

Este proyecto depende de:
- **Arduino/PlatformIO**: https://docs.platformio.org/
- **TFT_eSPI**: https://github.com/Bodmer/TFT_eSPI
- **WiFiManager**: https://github.com/tzapu/WiFiManager
- **NTPClient**: https://github.com/taranais/NtpClient
- **XPT2046_Touchscreen**: https://github.com/PaulStoffregen/XPT2046_Touchscreen

Si encuentras vulnerabilidades en estas librerías, recomendamos reportarlas a sus autores respectivos.

## Seguridad en Hardware

**Consideraciones de seguridad específicas del ESP32:**

- La memoria flash **NO es resistente a tampering** físico
- Credenciales WiFi se guardan en NVS (no encriptadas por defecto)
- Recomendamos usar WiFi WPA2 como mínimo
- No confíes en este dispositivo para datos críticos/confidenciales
- El ESP32 no tiene Secure Boot habilitado por defecto

## Buenas prácticas

Si utilizas Retro Clock en un proyecto:

1. **WiFi**: Usa contraseñas fuertes (>12 caracteres, mixto)
2. **Red**: Mantén el ESP32 en red privada / segura
3. **Updates**: Actualiza el firmware regularmente
4. **Firmware**: Verifica checksums SHA256 en releases
5. **Credenciales**: No hardcodees credenciales en el código

## Changelog de Seguridad

Ver [SECURITY_CHANGELOG.md](SECURITY_CHANGELOG.md) para historial de issues de seguridad reportados y resueltos.

## Contacto

- **Seguridad**: [tu_email@example.com](mailto:tu_email@example.com)
- **Otros issues**: Usa el [tracker de issues](https://github.com/wencescarlos/Retroclock/issues)
- **Discussions**: [Aquí](https://github.com/wencescarlos/Retroclock/discussions)

---

**Gracias por ayudar a mantener Retro Clock seguro.** 🔒
