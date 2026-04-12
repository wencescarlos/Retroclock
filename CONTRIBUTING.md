# Contribuir a Retro Clock 🎮

¡Gracias por tu interés en contribuir a Retro Clock! Este documento describe cómo puedes ayudar a mejorar el proyecto.

## Tabla de contenidos

1. [Código de Conducta](#código-de-conducta)
2. [Cómo Empezar](#cómo-empezar)
3. [Proceso de Contribución](#proceso-de-contribución)
4. [Convenciones de Código](#convenciones-de-código)
5. [Reportar Bugs](#reportar-bugs)
6. [Sugerir Mejoras](#sugerir-mejoras)
7. [Pull Requests](#pull-requests)

## Código de Conducta

Por favor, sé respetuoso con otros contribuidores. Nos comprometemos a mantener un espacio inclusivo y acogedor.

## Cómo Empezar

### Requisitos
- ESP32-2432S028Rv3 (o placa compatible)
- [PlatformIO](https://platformio.org/) instalado
- Git
- C++ básico

### Setup Local

```bash
# 1. Fork el repositorio en GitHub
# 2. Clona tu fork
git clone https://github.com/wencescarlos/Retroclock.git
cd Retroclock

# 3. Crea una rama de desarrollo
git checkout -b develop
git checkout -b feature/tu-feature

# 4. Abre el proyecto en PlatformIO
pio run -e CYD_RetroClock

# 5. Para flashear a la placa
pio run --target upload -e CYD_RetroClock

# 6. Monitor serie para debugging
pio device monitor -b 115200
```

## Proceso de Contribución

### 1. Crea una Issue primero
Antes de trabajar en algo importante, abre una Issue describiendo:
- Qué problema quieres resolver o qué feature quieres añadir
- Por qué es importante
- Tu propuesta de solución

### 2. Desarrolla en una rama separada
```bash
git checkout -b feature/nombre-descriptivo
# o para bugfixes:
git checkout -b bugfix/nombre-descriptivo
```

### 3. Haz commits atómicos
```bash
git commit -m "feat: añade soporte para...
git commit -m "fix: corrige problema en..."
git commit -m "docs: actualiza README"
git commit -m "refactor: mejora estructura de..."
```

**Prefijos recomendados:**
- `feat:` - Nueva funcionalidad
- `fix:` - Corrección de bug
- `docs:` - Cambios de documentación
- `refactor:` - Mejora de código existente
- `test:` - Añade o modifica tests
- `perf:` - Mejora de rendimiento

### 4. Pushea a tu fork
```bash
git push origin feature/nombre-descriptivo
```

### 5. Abre un Pull Request
- Describe qué cambios hiciste y por qué
- Referencia la Issue relacionada: `Closes #123`
- Espera revisión y retroalimentación

## Convenciones de Código

### C++ Style
- **Indentación**: 4 espacios (no tabs)
- **Línea máxima**: 100 caracteres
- **Nomenclatura**:
  - Variables: `camelCase` → `miVariable`
  - Constantes: `UPPER_CASE` → `MAX_BRILLO`
  - Funciones: `camelCase` → `leerTouch()`
  - Clases: `PascalCase` → `TetrísReloj`

### Ejemplo
```cpp
// Variables y funciones
uint8_t brillo = 200;
void configurarPantalla(uint16_t ancho, uint16_t alto) {
    // Código
}

// Constantes
#define MAX_ZONA 12
#define MIN_ZONA -12

// Structs
struct ConfiguracionWiFi {
    char ssid[32];
    char password[64];
};
```

### Documentación de Código
```cpp
/**
 * Lee la entrada del panel táctil
 * 
 * @return Struct con coordenadas (x, y) y estado pulsado
 */
Punto leerTouch() {
    // Implementación
}
```

## Reportar Bugs

Usa la template de **Bug Report** en Issues. Incluye:

- **Descripción**: ¿Qué falla?
- **Pasos para reproducir**: Instrucciones específicas
- **Comportamiento esperado**: Qué debería ocurrir
- **Comportamiento actual**: Qué ocurre realmente
- **Capturas/Logs**: Si es posible, incluye capturas o logs de serie
- **Placa y entorno**: Modelo exacto de placa, versión PlatformIO, etc.

**Ejemplo:**
```markdown
**Descripción**: La hora se desincroniza después de cambiar de zona horaria

**Pasos para reproducir**:
1. Enciende el reloj
2. Accede al menú (mantén pulsado 2s)
3. Cambia la zona horaria de UTC+0 a UTC+1
4. Observa la hora mostrada

**Comportamiento esperado**: La hora debería actualizar a UTC+1 correctamente

**Comportamiento actual**: La hora muestra UTC+0

**Placa**: ESP32-2432S028Rv3
**PlatformIO**: 6.1.0
```

## Sugerir Mejoras

Usa la template de **Feature Request** en Issues:

- **Descripción**: ¿Qué nueva característica?
- **Caso de uso**: ¿Por qué la necesitas?
- **Alternativas consideradas**: ¿Hay otras soluciones?
- **Contexto adicional**: Cualquier otra información

## Pull Requests

### Antes de enviar

- [ ] Código compilado sin warnings
- [ ] Probado en la placa ESP32
- [ ] Sigue las convenciones de código
- [ ] Añade comentarios donde sea necesario
- [ ] Actualiza el README si es necesario
- [ ] Actualiza CHANGELOG.md

### Info requerida en cada PR

```markdown
## Descripción
Breve descripción de los cambios

## Tipo de cambio
- [ ] Bug fix
- [ ] Nueva feature
- [ ] Breaking change
- [ ] Mejora de documentación

## Closed Issues
Closes #123

## Testing
Describe cómo testear estos cambios

## Screenshots/Videos (si aplica)
Incluye evidencia de que funciona
```

### Ciclo de revisión

1. **Automático**: GitHub Actions ejecuta tests y validación
2. **Revisor**: Alguien del equipo revisará el código
3. **Cambios solicitados**: Si es necesario, haremos sugerencias
4. **Merge**: Una vez aprobado, se mezcla a `develop`

## Áreas donde podemos ayuda 🙏

- ✅ Nuevas animaciones/modos de reloj
- ✅ Optimización de rendimiento
- ✅ Traducción de documentación
- ✅ Mejoras en la interfaz táctil
- ✅ Casos de uso y tutoriales
- ✅ Corrección de bugs reportados
- ✅ Mejora de tests
- ✅ Documentación

## Desarrollo Local - Troubleshooting

### Problema: "platformio not found"
```bash
pip install platformio
pio --version
```

### Problema: Compilación falla
```bash
# Limpia build anteriores
platformio run --target clean

# Recompila
platformio run -e CYD_RetroClock
```

### Problema: PR no pasa CI
- Revisa los logs de GitHub Actions
- Ejecuta localmente: `platformio run -e CYD_RetroClock`
- Asegúrate de seguir el estilo de código

## Preguntas?

- **Issues**: Para bugs y features
- **Discussions**: Para preguntas generales
- **Email**: Si tienes algo confidencial

¡Gracias por contribuir! 🚀
