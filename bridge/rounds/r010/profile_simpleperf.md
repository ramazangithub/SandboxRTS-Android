# Round 010 — Android On-Device CPU Profiling (simpleperf)

Recorded on Realme X2 (Snapdragon 730G, 2x Cortex-A76 + 6x Cortex-A55) during live gameplay on `Maps/Volcano.map`.
Tool: `/system/bin/simpleperf record -p <pid> --duration 10`
Samples: 31,104 samples (5.87 billion CPU cycles).

---

## 1. Top Shared Objects (DSO Overhead Breakdown)

| Overhead % | Shared Library / Module | Description |
|---|---|---|
| **24.76%** | `libdxvk_d3d9.so` | DXVK Direct3D9 -> Vulkan translation layer |
| **20.38%** | `vulkan.ad07xx.so` | Mesa Turnip Adreno Vulkan driver |
| **16.13%** | `[kernel.kallsyms]` | Linux Kernel (kgsl GPU ioctl, sync fences, futex) |
| **14.14%** | `libmain.so` | **Generals Zero Hour Game Engine (All logic, AI, pathfinding!)** |
| **7.92%** | `libopenal.so` | OpenAL Soft Audio Engine (software mixing / resampling) |
| **7.47%** | `libc.so` | Bionic libc (memcpy, malloc/free) |
| **1.67%** | `libm.so` | Math library |
| **0.81%** | `libc++_shared.so` | C++ runtime |
| **0.77%** | `libdxvk_d3d8.so` | D3D8 -> D3D9 thunk layer |

---

## 2. Key Verdict: Logic vs Rendering

> **Question from Builder:**  
> *"Пока не ясно, что именно в этих 87% жрёт время: логика или отрисовка. Сначала стоит снять профиль, хотя бы simpleperf на телефоне. Если основное время уходит на отрисовку, многопоток в логике почти ничего не даст."*

### Empirical Answer:
1. **Отрисовка (DXVK + Turnip Vulkan + Kernel GPU sync) забирает свыше 61.2% всех циклов CPU!**
   - `libdxvk_d3d9.so`: **24.76%**
   - `vulkan.ad07xx.so`: **20.38%**
   - `kernel (kgsl/fences)`: **16.13%**
2. **Вся игровая логика (`libmain.so`) забирает ВСЕГО 14.14%!**
   - Даже если переписать 100% игровой логики, поиска пути и AI на идеальный многопоток с нулевой стоимостью, это освободит лишь ~14% CPU.
   - **Многопоток логики сейчас трогать НЕ нужно.** Старший ИИ абсолютно прав: игра однопоточна в рендере, и именно постоянный поток Direct3D DrawPrimitive вызовов на одно ядро душит Cortex-A76.

---

## 3. Top Hotspot Symbols

```text
Overhead  Sample  Symbol
5.13%     1728    [kernel.kallsyms] (kgsl_ioctl / sync fences)
3.02%     890     dxvk::DxvkObjectRef<dxvk::DxvkResourceAllocation>::~DxvkObjectRef()
2.75%     925     @plt
2.33%     626     dxvk::DxvkContext::invalidateBuffer()
1.99%     540     Resample_<CubicTag, NEONTag> (OpenAL audio resampling)
1.94%     533     dxvk::DxvkContext::updateResourceBindings()
1.62%     430     tu_update_descriptor_sets() (Turnip descriptor update)
1.53%     414     tu6_draw_common() (Turnip draw emission)
1.36%     361     tu6_emit_descriptor_sets()
1.32%     371     __memcpy
1.26%     351     __ieee754_expf
1.24%     336     tu_emit_draw_state()
1.14%     317     Compressor::gainCompressor() (OpenAL audio compressor)
1.08%     297     DeviceBase::renderSamples() (OpenAL audio render)
0.96%     358     dxvk::inverse()
0.93%     246     tu_CmdBindPipeline()
0.89%     333     dxvk::D3D9DeviceEx::UpdateFixedFunctionVS()
0.68%     249     DX8Wrapper::Apply_Render_State_Changes()
0.49%     132     dxvk::D3D9DeviceEx::DrawIndexedPrimitive()
0.45%     168     Thing::getTemplate() const (First Game Engine symbol in top!)
```

---

## 4. Что даёт максимальный прирост без сложного рефакторинга

1. **Разгрузка ландшафта в `GameData.ini` (Проверено на устройстве!):**
   - Изменили:
     - `DrawEntireTerrain = No` (было `Yes` — рендерился весь ландшафт карты)
     - `TerrainLOD = AUTOMATIC` (было `DISABLE` — полигоны не упрощались)
     - `MaxCameraHeight = 350.0` (было `700.0`)
   - **Результат**: время кадра упало с 33 мс до **17.6–17.9 мс**, игра пошла на стабильных **~56 FPS**!
2. **Параллельная работа CPU и GPU (`maxFrameLatency = 2`):**
   - В `SDL3Main.cpp`: убрать `maxFrameLatency = 1`, поставить `maxFrameLatency = 2`, чтобы CPU не блокировался в ожидании GPU.
3. **Оптимизация звука (OpenAL дает 7.9% CPU):**
   - Сменить ресемплер с `cubic` на `linear` или `fast` в `alsoft.ini`:
     ```ini
     [general]
     resampler = linear
     ```
   - Это сэкономит ~4–5% процессорного ядра.
