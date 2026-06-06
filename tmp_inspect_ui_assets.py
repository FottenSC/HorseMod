import os
import sys

sys.path.insert(0, r"E:\myMods\tools\moveset_parser")
import uassetparse as u

paths = [
    r"E:\myMods\dump\SoulcaliburVI\Content\UI\Data\GameData\InBattleMenu\ElementConfig\LuxTrainingFightRequestRankMatchResultMenuWindowConfig_ui1.uasset",
    r"E:\myMods\dump\SoulcaliburVI\Content\UI\MenuElement\MenuWindow\RankedMatchWindow\LuxRankedMatchFightRequestWindow.uasset",
    r"E:\myMods\dump\SoulcaliburVI\Content\UI\MenuElement\Panels\FightRequest\LuxFightRequestPanel.uasset",
    r"E:\myMods\dump\SoulcaliburVI\Content\UI\Data\GameData\FightRequest\LuxFightRequestData_ui.uasset",
]

if len(sys.argv) > 1 and sys.argv[1] == "scan":
    root = r"C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump\SoulcaliburVI\Content\UI"
    needles = ["qualityOfService", "requestSide", "displayCharacterName", "fightRequestSetting", "LuxFightRequestData"]
    for dirpath, _dirs, files in os.walk(root):
        for file in files:
            if not file.endswith(".uasset"):
                continue
            path = os.path.join(dirpath, file)
            uexp = path[:-7] + ".uexp"
            if not os.path.exists(uexp):
                continue
            try:
                pkg = u.parse_uasset(path)
                props = u.parse_uexp(uexp, pkg)
            except Exception:
                continue
            data = props.get("Data")
            if not isinstance(data, list):
                continue
            decoded = bytes((b + 1) & 0xFF for b in data).decode("utf-8", errors="replace")
            if any(needle in decoded for needle in needles):
                print("---", path)
                print(decoded[:5000])
    raise SystemExit

keys = [
    "rank", "fight", "stand", "list", "content", "reset", "connection",
    "prefer", "delay", "input", "menu", "window", "config", "description",
    "disabled", "personal", "character", "side",
]

for path in paths:
    print("---", path)
    pkg = u.parse_uasset(path)
    print("names", len(pkg.name_table), "imports", len(pkg.imports), "exports", len(pkg.exports))
    uexp = path[:-7] + ".uexp"
    try:
        props = u.parse_uexp(uexp, pkg)
        print("props", props)
        data = props.get("Data")
        if isinstance(data, list):
            decoded = bytes((b + 1) & 0xFF for b in data).decode("utf-8", errors="replace")
            print("decoded-data")
            print(decoded)
    except Exception as exc:
        print("props-error", type(exc).__name__, exc)
    try:
        rows = u.parse_datatable(uexp, pkg)
        print("rows", rows)
    except Exception as exc:
        print("rows-error", type(exc).__name__, exc)
    for name in pkg.name_table:
        low = name.lower()
        if any(key in low for key in keys):
            print(name)
