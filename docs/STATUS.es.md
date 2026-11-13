# Estado e historial

## Estado (noviembre de 2026)

**Recreativa (Hikaru)**: jugable de principio a fin: demo, menús, las cuatro pistas, menú de servicio con
ajustes guardados, sonido de las dos AICA, capas 2D, iluminación, niebla, polvo, destellos y desenfoque del turbo.
Pendiente: brillos especulares, efectos DSP del audio (reverb), juego en red entre muebles.

**Dreamcast (PAL)**: jugable sobre Flycast con nuestro núcleo SH-4; panorámico por gancho de cámara.
Pendiente: los marcadores 2D salen estirados en panorámico; un arco azul en el cielo durante el vuelo previo a
la carrera.

No probado por el autor del código en Windows real: los binarios de Windows se compilan en cruzado y los prueba
el usuario.

## Historial

1. Análisis del romset; arranque de la BIOS y del programa con un intérprete SH-4 propio.
2. Recompilación por bloques (AOT) con verificación por hash y respaldo al intérprete; dos CPU en paralelo.
3. Procesador de comandos y GPU de Hikaru; renderizador por software de referencia.
4. Renderizador Vulkan y primera build de Windows.
5. Sonido: ARM7 + AICA propios ×2 con el driver original.
6. Capas 2D (Eurasia) y cielo.
7. Iluminación por vértice y niebla.
8. Correcciones 2D, menú test y rendimiento.
9. Controles, prioridades 3D y rendimiento (GPU en paralelo con la CPU).
10. Menú de opciones, panorámico 16:9, mezcla aditiva.
11. Persistencia (ini/nv), capturas, prioridad 2D/3D.
12. EEPROM del MIE (el menú de servicio guarda), región, pilotos ocultos, MSAA, mando XInput.
13. Corregido el bloqueo de la demo (contador de ciclos del Z80 de 32 bits en Windows); frontend SDL2 para
    Linux / Steam Deck; lanzador.
14. Recompilación de Dreamcast sobre Flycast.
15. Más bloques de Dreamcast, panorámico de Dreamcast, iconos.
16. Distribución sin código del juego (motor + constructor que genera el módulo en el PC del usuario),
    interfaz en español e inglés, desenfoque del turbo, repositorio público.
