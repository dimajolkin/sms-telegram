"""Расстановка вендорских моделей в корпусе sms-telegram (Fusion 360).

Запуск:
    python3 ~/.cursor/skills/fusion-mcp-modeling/scripts/fusion_exec.py \
        hardware/enclosure/scripts/place_parts.py

Скрипт идемпотентный: гоняй его после любой правки геометрии корпуса.
Правки эскизов/экструзий пересчитывают таймлайн, а положение вставленных
компонентов в нём не хранится, поэтому они возвращаются в начало координат.

Чтобы положение сохранилось в документе, после этого скрипта нажми в Fusion
кнопку "Capture Position" (или Position -> Capture Position) и проверь, что
ни одно тело сборки не осталось в нуле.
"""
import adsk.core, adsk.fusion

# Ориентация вставленных STEP: локальные оси -> мировые
XIAO_MAT = [0, 0, 1, 0,
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 0, 1]          # USB к вырезу (+Y)
TFT_MAT = [0, -1, 0, 0,
           1, 0, 0, 0,
           0, 0, 1, 0,
           0, 0, 0, 1]           # Rz+90, стекло вверх

XIAO_RAIL_Z = 3.5                # верх рельс под текстолит XIAO
XIAO_Y_CENTER = 66.5
TFT_POST_Z = 12.0                # верх стоек под плату дисплея
TFT_WINDOW_CENTER = (0.1, 51.5)  # центр окна крышки, по нему центрируется модуль экрана


def run(_context: str):
    app = adsk.core.Application.get()
    des = adsk.fusion.Design.cast(app.activeProduct)
    root = des.rootComponent

    def world_bb(body):
        b = body.boundingBox
        return (b.minPoint.x * 10, b.maxPoint.x * 10,
                b.minPoint.y * 10, b.maxPoint.y * 10,
                b.minPoint.z * 10, b.maxPoint.z * 10)

    def visible_bbs(occ):
        """Мировые bbox всех видимых тел. Прокси уже в контексте сборки —
        трансформ occurrence применять повторно нельзя."""
        rows = []

        def walk(o):
            for i in range(o.bRepBodies.count):
                body = o.bRepBodies.item(i)
                if body.isVisible:
                    rows.append(world_bb(body))
            for i in range(o.childOccurrences.count):
                child = o.childOccurrences.item(i)
                if child.isLightBulbOn:
                    walk(child)

        walk(occ)
        return rows

    def largest(occ, pred):
        best, area = None, 0
        for x in visible_bbs(occ):
            a = (x[1] - x[0]) * (x[3] - x[2])
            if pred(x) and a > area:
                best, area = x, a
        return best

    def pcb(occ, min_side):
        return largest(occ, lambda x: (x[1] - x[0]) > min_side
                       and (x[3] - x[2]) > min_side
                       and (x[5] - x[4]) < 3.5)

    def display_module(occ):
        return largest(occ, lambda x: (x[1] - x[0]) > 25
                       and 15 < (x[3] - x[2]) < 22
                       and (x[5] - x[4]) > 2)

    def set_matrix(occ, arr):
        m = adsk.core.Matrix3D.create()
        m.setWithArray(arr)
        occ.transform2 = m

    def move(occ, dx, dy, dz):
        m = occ.transform.copy()
        m.translation = adsk.core.Vector3D.create(
            m.translation.x + dx / 10,
            m.translation.y + dy / 10,
            m.translation.z + dz / 10)
        occ.transform2 = m

    def hide_pin_headers(occ):
        def walk(o):
            if "pin header" in o.component.name.lower():
                o.isLightBulbOn = False
            for i in range(o.childOccurrences.count):
                walk(o.childOccurrences.item(i))
        walk(occ)

    xiao = tft = None
    for i in range(root.occurrences.count):
        o = root.occurrences.item(i)
        name = o.component.name.upper()
        if "XIAO" in name:
            xiao = o
        elif "TFT" in name or "ER-" in name:
            tft = o
    if xiao is None or tft is None:
        raise RuntimeError(f"не найдены компоненты: xiao={xiao}, tft={tft}")

    # ---- XIAO ESP32-S3: текстолитом на рельсы ----
    set_matrix(xiao, XIAO_MAT)
    p = pcb(xiao, 12)
    move(xiao,
         -(p[0] + p[1]) / 2,
         XIAO_Y_CENTER - (p[2] + p[3]) / 2,
         XIAO_RAIL_Z - p[4])

    # ---- TFT: платой на стойки, экраном по центру окна ----
    set_matrix(tft, TFT_MAT)
    hide_pin_headers(tft)
    p = pcb(tft, 18)
    move(tft, 0, 0, TFT_POST_Z - p[4])
    g = display_module(tft)
    move(tft,
         TFT_WINDOW_CENTER[0] - (g[0] + g[1]) / 2,
         TFT_WINDOW_CENTER[1] - (g[2] + g[3]) / 2,
         0)
    p = pcb(tft, 18)
    if abs(p[4] - TFT_POST_Z) > 0.01:
        move(tft, 0, 0, TFT_POST_Z - p[4])

    # ---- отчёт ----
    xp = pcb(xiao, 12)
    tp = pcb(tft, 18)
    tg = display_module(tft)
    print("XIAO pcb   x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % xp)
    print("TFT  pcb   x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % tp)
    print("TFT  экран x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f" % tg)
    print("зазор XIAO/рельсы %.2f мм" % (xp[4] - XIAO_RAIL_Z))
    print("зазор TFT/стойки  %.2f мм" % (tp[4] - TFT_POST_Z))

    stray = [b for occ in (xiao, tft) for b in visible_bbs(occ)
             if b[4] < 0 or b[3] < 20]
    print("тела вне корпуса:", len(stray))
    for s in stray[:5]:
        print("   x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f" % s)
