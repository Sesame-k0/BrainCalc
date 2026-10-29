# -*- coding: utf-8 -*-
"""
Generates help_dict.json (with English & Japanese side-by-side for easy maintenance)
and compiles it into help_data.h for Lilium (BrainCalc) right sidebar Japanese help display.

Formatting rules:
- 1 line max = 8 full-width chars (or 16 half-width units).
- Mathematical expressions / formulas / syntax strings are never broken in the middle.
- Both English text (en_keys, en_desc) and Japanese text (ja_title, ja_desc) are stored
  side-by-side in help_dict.json so the dictionary can be easily maintained.
"""

import os
import re
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def char_unit_width(ch):
    """Returns 1 for ASCII half-width characters, 2 for full-width characters."""
    code = ord(ch)
    if 0x0020 <= code <= 0x007E:
        return 1
    return 2


def is_formula_line(line):
    """Returns True if a line is primarily a mathematical formula or command syntax."""
    s = line.strip()
    if not s:
        return False
    has_ja = any(
        (0x3000 <= ord(c) <= 0x30FF) or
        (0x4E00 <= ord(c) <= 0x9FFF) or
        (0xFF00 <= ord(c) <= 0xFFEF)
        for c in s
    )
    if not has_ja:
        return True
    return False


# Regex matching an indivisible inline formula/ASCII token
INLINE_TOKEN_RE = re.compile(
    r"[A-Za-z0-9_+\-*/^=<>!~.\[\](),:;°μσθπλ]+"
)


def tokenize_ja_line(line):
    """
    Splits a line into tokens where contiguous ASCII/formula expressions stay together
    as a single indivisible token, and Japanese characters are individual 1-char tokens.
    """
    tokens = []
    pos = 0
    for m in INLINE_TOKEN_RE.finditer(line):
        start, end = m.span()
        while pos < start:
            tokens.append(line[pos])
            pos += 1
        tokens.append(m.group(0))
        pos = end
    while pos < len(line):
        tokens.append(line[pos])
        pos += 1
    return tokens


def wrap_line_8fw(line, max_units=16):
    """
    Wraps a single line so each resulting line is at most 8 full-width characters
    (16 half-width units), without breaking formulas or ASCII tokens in the middle.
    """
    s = line.strip()
    if not s:
        return [""]

    total_w = sum(char_unit_width(c) for c in s)
    if total_w <= max_units or is_formula_line(s):
        return [s]

    tokens = tokenize_ja_line(s)
    out_lines = []
    cur = ""
    cur_w = 0

    for tok in tokens:
        tok_w = sum(char_unit_width(c) for c in tok)
        if cur == "" and tok == " ":
            continue
        if cur_w + tok_w <= max_units:
            cur += tok
            cur_w += tok_w
        else:
            if cur:
                out_lines.append(cur.rstrip())
            cur = tok.lstrip()
            cur_w = sum(char_unit_width(c) for c in cur)

    if cur:
        out_lines.append(cur.rstrip())

    return out_lines if out_lines else [""]


def is_standalone_line(s):
    """
    Returns True if line `s` is a standalone item (bullet, section header,
    key:value pair, or pure formula) that should not be merged with adjacent lines.
    """
    if not s:
        return True
    if is_formula_line(s):
        return True
    if s.startswith(("・", "※", "■", "◆")):
        return True
    if s.startswith("【") and s.endswith("】"):
        return True
    if ":" in s or "：" in s:
        return True
    return False


def format_ja_desc(desc, max_units=None):
    """
    Normalizes a Japanese description by joining artificial mid-sentence line breaks
    while preserving intentional paragraph, bullet, header, and formula breaks.
    Actual pixel-width line wrapping is performed dynamically by display.cpp based on
    the text box width.
    """
    raw_lines = [rl.strip() for rl in desc.split("\n") if rl.strip()]
    if not raw_lines:
        return ""

    paragraphs = [raw_lines[0]]
    for line in raw_lines[1:]:
        prev = paragraphs[-1]
        # Keep separate line if previous ended a sentence, or either is a standalone line,
        # or if `line` starts with 【...】 shortcut and `prev` is NOT mid-clause
        prev_continues = prev.endswith(("、", "に", "で", "を", "の", "が", "は", "と", "や", "へ", "て", "し"))
        line_is_shortcut = line.startswith("【") and not prev_continues and not line.endswith(("、", "に", "で", "を", "の", "が", "は", "と"))
        prev_is_shortcut = prev.startswith("【") and not prev.endswith(("。", "、", "に", "で", "を", "の", "が", "は", "と"))
        if (
            prev.endswith(("。", "！", "？"))
            or is_standalone_line(prev)
            or is_standalone_line(line)
            or line_is_shortcut
            or prev_is_shortcut
        ):
            paragraphs.append(line)
        else:
            if prev[-1].isascii() and line[0].isascii():
                paragraphs[-1] = prev + " " + line
            else:
                paragraphs[-1] = prev + line

    return "\n".join(paragraphs)


RAW_HELP_ENTRIES = [
    # =========================================================================
    # 1. ホーム画面アプリ (Home Screen Applications)
    # =========================================================================
    {
        "id": "App_Calculation",
        "en_keys": ["Calculation", "CALCULATION"],
        "en_desc": "Standard arithmetic, fractions, powers, roots, trigonometry, calculus, and matrices.",
        "ja_title": "計算アプリ",
        "ja_desc": "四則演算や分数、\n微積分、三角関数\n行列などの計算を\n行います。\n【Enter】計算実行\n【F1】関数メニュー\n【F9】変数一覧"
    },
    {
        "id": "App_Functions",
        "en_keys": ["Functions", "FUNCTIONS"],
        "en_desc": "Define functions f(x), plot graphs, find roots/extrema/intersections, and view value tables.",
        "ja_title": "関数グラフ",
        "ja_desc": "関数f(x)を定義し\nグラフ描画や数値\n表を作成します。\n・交点や極値の探索\n・接線や定積分計算\n・極座標/媒介変数"
    },
    {
        "id": "App_Sequences",
        "en_keys": ["Sequences", "SEQUENCES"],
        "en_desc": "Define explicit or recursive sequences u(n), plot terms, view tables, and compute sums.",
        "ja_title": "数列アプリ",
        "ja_desc": "一般項や漸化式で\n数列u(n),v(n)を\n定義します。\n・u(n+1)漸化式対応\n・項のグラフと数表\n・指定区間の総和"
    },
    {
        "id": "App_Statistics",
        "en_keys": ["Statistics", "STATISTICS"],
        "en_desc": "Analyze 1-variable datasets with histograms, box plots, mean, standard deviation, and quartiles.",
        "ja_title": "統計アプリ",
        "ja_desc": "データと度数を入力\nし統計量を算出。\n・ヒストグラム描画\n・箱ひげ図の表示\n・平均/分散/標準偏差\n・中央値/四分位数"
    },
    {
        "id": "App_Regression",
        "en_keys": ["Regression", "REGRESSION"],
        "en_desc": "Two-variable scatter plots and regression models (linear, polynomial, exponential, logarithmic).",
        "ja_title": "回帰分析",
        "ja_desc": "2変数データ(X,Y)\nの散布図と回帰式\nを計算します。\n・一次/多項式回帰\n・指数/対数/累乗\n・相関係数rと予測"
    },
    {
        "id": "App_Probability",
        "en_keys": ["Probability", "PROBABILITY"],
        "en_desc": "Compute probabilities and inverses for Binomial, Normal, Student's t, Poisson, Chi-squared, etc.",
        "ja_title": "確率分布",
        "ja_desc": "各種確率分布の確率\nとグラフを計算。\n・正規分布/t分布\n・二項/ポアソン分布\n・カイ二乗/F分布\n・区間確率と逆確率"
    },
    {
        "id": "App_Solver",
        "en_keys": ["Equations", "EQUATIONS", "Solver"],
        "en_desc": "Solve polynomial equations, general numerical equations, and systems of linear equations.",
        "ja_title": "方程式ソルバー",
        "ja_desc": "方程式の求根および\n連立方程式を解く\nアプリです。\n・2次/3次方程式の解\n・連立一次方程式\n・数値解の区間探索"
    },
    {
        "id": "App_RPN",
        "en_keys": ["RPN", "RPN Calculator"],
        "en_desc": "Reverse Polish Notation stack calculator.",
        "ja_title": "RPN電卓",
        "ja_desc": "逆ポーランド記法の\nスタック電卓です。\n数値入力後に\n【Enter】で積み、\n演算子キーで直前の\n要素を計算します。"
    },
    {
        "id": "App_Atomic",
        "en_keys": ["Periodic table", "Periodic Table", "Elements"],
        "en_desc": "Interactive periodic table of chemical elements with atomic mass and properties.",
        "ja_title": "元素周期表",
        "ja_desc": "全118元素の周期表\nと性質を閲覧。\n・原子番号と元素記号\n・モル質量(原子量)\n・電気陰性度と分類\n・電子配置の確認"
    },
    {
        "id": "App_Python",
        "en_keys": ["Python", "PYTHON"],
        "en_desc": "MicroPython script editor and interactive shell with math, matplotlib, turtle, and kandinsky.",
        "ja_title": "Python",
        "ja_desc": "Pythonスクリプトの\n作成と実行を行う\nプログラミング環境\nです。\n・math/cmath計算\n・グラフや図形描画"
    },
    {
        "id": "App_Reader",
        "en_keys": ["Reader", "READER", "Document Reader"],
        "en_desc": "View text documents stored in the archive.",
        "ja_title": "リーダー",
        "ja_desc": "内蔵テキストや文書\nファイルを閲覧する\nリーダーアプリです"
    },
    {
        "id": "App_External",
        "en_keys": ["External", "EXTERNAL"],
        "en_desc": "Run external add-in applications.",
        "ja_title": "外部アプリ",
        "ja_desc": "追加の外部アドイン\nアプリを起動します"
    },
    {
        "id": "App_Settings",
        "en_keys": ["Settings", "SETTINGS"],
        "en_desc": "Configure angle units, number format, writing format, complex format, and system info.",
        "ja_title": "環境設定",
        "ja_desc": "電卓の計算動作や\n表示形式を設定。\n・角度(度/ラジアン)\n・小数/科学/工学表記\n・2D自然表示/1行\n・複素数形式と情報"
    },

    # =========================================================================
    # 2. 設定画面 (Settings Menu & Options)
    # =========================================================================
    {
        "id": "Set_MathOptions",
        "en_keys": ["Math options"],
        "en_desc": "Configure angle measure, result format, writing format, complex format, and multiplication sign.",
        "ja_title": "計算オプション",
        "ja_desc": "数学計算の表示や\n単位を設定します。\n・角度の単位\n・計算結果の表示形式\n・数式の入力形式\n・複素数の表示形式\n・掛け算記号の種類"
    },
    {
        "id": "Set_AngleUnit",
        "en_keys": ["Angle measure"],
        "en_desc": "Select the angle unit used in sin, cos, tan, and polar coordinates.",
        "ja_title": "角度の単位",
        "ja_desc": "三角関数や極座標で\n使う角度の単位を\n設定します。\n・Degrees: 度(°)\n・Radians: 弧度(rad)\n・Gradians: グラード"
    },
    {
        "id": "Set_Degrees",
        "en_keys": ["Degrees"],
        "en_desc": "1 full circle = 360 degrees (right angle = 90 degrees).",
        "ja_title": "度 (Degrees)",
        "ja_desc": "円周を360°、直角を\n90°とする単位です。\n日常計算や幾何学で\n標準的に使います。"
    },
    {
        "id": "Set_Radians",
        "en_keys": ["Radians"],
        "en_desc": "1 full circle = 2*pi radians (right angle = pi/2).",
        "ja_title": "ラジアン(弧度)",
        "ja_desc": "円周を 2π rad、\n直角を π/2 とする\n弧度法です。\n微積分や高校・大学\n数学の標準単位です"
    },
    {
        "id": "Set_Gradians",
        "en_keys": ["Gradians"],
        "en_desc": "1 full circle = 400 gradians (right angle = 100 gon).",
        "ja_title": "グラード(gon)",
        "ja_desc": "直角を100gon、\n円周を400gonとする\n測量分野などで使わ\nれる角度単位です。"
    },
    {
        "id": "Set_DisplayMode",
        "en_keys": ["Result format", "Display mode"],
        "en_desc": "Choose how numerical results are formatted: Decimal, Scientific, Engineering, or Significant figures.",
        "ja_title": "結果の表示形式",
        "ja_desc": "計算結果の数値表記\nを選択します。\n・Decimal: 通常小数\n・Scientific: 科学\n・Engineering: 工学\n・有効数字の桁指定"
    },
    {
        "id": "Set_Decimal",
        "en_keys": ["Decimal"],
        "en_desc": "Standard decimal notation (switches to scientific only for very large/small numbers).",
        "ja_title": "通常小数表記",
        "ja_desc": "一般的な10進小数で\n結果を表示します。\n【例】\n1234.56\n0.0078"
    },
    {
        "id": "Set_Scientific",
        "en_keys": ["Scientific", "Scientific notation"],
        "en_desc": "Scientific exponential notation a * 10^n (1 <= |a| < 10).",
        "ja_title": "科学表記 (Sci)",
        "ja_desc": "常に a×10^n の\n指数形式で数値を\n表示します。\n【例】\n1.23456ᴇ3"
    },
    {
        "id": "Set_Engineering",
        "en_keys": ["Engineering"],
        "en_desc": "Engineering notation where the power of 10 is always a multiple of 3.",
        "ja_title": "工学表記 (Eng)",
        "ja_desc": "10の指数が必ず3の\n倍数(k,M,m,μ対応)\nになる形式で表示\nします。\n【例】\n1.234ᴇ3, 780ᴇ-6"
    },
    {
        "id": "Set_SignificantFigures",
        "en_keys": ["Significant figures"],
        "en_desc": "Set the maximum number of significant digits displayed in results.",
        "ja_title": "有効数字の桁数",
        "ja_desc": "計算結果を表示する\n際の有効数字の最大\n桁数を設定します。\n(標準は10桁です)"
    },
    {
        "id": "Set_EditionMode",
        "en_keys": ["Writing format"],
        "en_desc": "Choose between 2D natural textbook display and 1-line linear input.",
        "ja_title": "数式の入力形式",
        "ja_desc": "数式の入力・表示\n方法を選択します。\n・Natural: 2D教科書\n　通りの自然表示\n・Linear: 1行入力"
    },
    {
        "id": "Set_Edition2D",
        "en_keys": ["Natural", "2D natural math"],
        "en_desc": "Natural 2D textbook display for fractions, roots, powers, and integrals.",
        "ja_title": "自然表示 (2D)",
        "ja_desc": "分数やルート、累乗\n定積分などを教科書\n通りの2次元形式で\n視覚的に入力します"
    },
    {
        "id": "Set_EditionLinear",
        "en_keys": ["Linear", "Linear (1-line)"],
        "en_desc": "Single-line text input mode like a programming language (or linear regression y=ax+b).",
        "ja_title": "1行入力 / 線形",
        "ja_desc": "【設定】数式を1行の\n文字列で入力します\n【回帰】1次関数\ny=a·x+b による\n線形回帰を行います"
    },
    {
        "id": "Set_ComplexFormat",
        "en_keys": ["Complex format"],
        "en_desc": "Select complex number mode: Real only, Cartesian (a+bi), or Polar (r*e^(i*theta)).",
        "ja_title": "複素数の形式",
        "ja_desc": "複素数の扱いと表示\n形式を選択します。\n・Real: 実数のみ\n・Cartesian: a+bi\n・Polar: r·e^(iθ)"
    },
    {
        "id": "Set_Real",
        "en_keys": ["Real"],
        "en_desc": "Real numbers only; square roots of negative numbers yield Unreal result.",
        "ja_title": "実数のみ(Real)",
        "ja_desc": "実数の範囲のみで\n計算を行います。\n負の数の平方根など\nは非実数となります"
    },
    {
        "id": "Set_Cartesian",
        "en_keys": ["Cartesian", "Cartesian (a+bi)"],
        "en_desc": "Display complex numbers in rectangular form a + bi (or Cartesian curve y=f(x)).",
        "ja_title": "直交形式(a+bi)",
        "ja_desc": "複素数を実部と虚部\nによる a+bi 形式で\n表示します。\n(グラフでは直交座標\ny=f(x)を表します)"
    },
    {
        "id": "Set_Polar",
        "en_keys": ["Polar", "Polar (r*e^(i*theta))"],
        "en_desc": "Display complex numbers in polar exponential form r*e^(i*theta) (or polar curve r=f(theta)).",
        "ja_title": "極形式 r·e^(iθ)",
        "ja_desc": "複素数を絶対値rと\n偏角θの極形式で\n表示します。\n(グラフでは極座標\nr=f(θ)を表します)"
    },
    {
        "id": "Set_Parametric",
        "en_keys": ["Parametric"],
        "en_desc": "Parametric curve defined by x(t) and y(t).",
        "ja_title": "媒介変数表示",
        "ja_desc": "パラメータtを用いた\n媒介変数曲線\n(x(t), y(t)) を\n定義・描画します。"
    },
    {
        "id": "Set_SymbolMultiplication",
        "en_keys": ["Multiply", "Multiplication symbol"],
        "en_desc": "Select the symbol displayed for multiplication: Cross, Dot, Star, or Auto.",
        "ja_title": "掛け算の記号",
        "ja_desc": "乗算(掛け算)に使う\n表示記号を選びます\n・Cross: × (クロス)\n・Dot: · (中黒)\n・Star: * (星印)\n・Auto: 自動選択"
    },
    {
        "id": "Set_MultCross",
        "en_keys": ["Cross"],
        "en_desc": "Display multiplication as a cross sign (x).",
        "ja_title": "× (クロス記号)",
        "ja_desc": "掛け算を教科書の\n「×」記号で表示\nします。"
    },
    {
        "id": "Set_MultDot",
        "en_keys": ["Dot"],
        "en_desc": "Display multiplication as a middle dot (·).",
        "ja_title": "· (ドット記号)",
        "ja_desc": "掛け算を中黒「·」\n記号で表示します。"
    },
    {
        "id": "Set_MultStar",
        "en_keys": ["Star"],
        "en_desc": "Display multiplication as an asterisk (*).",
        "ja_title": "* (アスタリスク)",
        "ja_desc": "掛け算をプログラム\n風の「*」記号で\n表示します。"
    },
    {
        "id": "Set_MultAuto",
        "en_keys": ["Auto"],
        "en_desc": "Automatically choose the cleanest multiplication symbol based on context.",
        "ja_title": "自動選択(Auto)",
        "ja_desc": "式の文脈に応じて\n見やすい掛け算記号\nを自動で選びます。"
    },
    {
        "id": "Set_SymbolFunction",
        "en_keys": ["Expression format"],
        "en_desc": "Choose how function names are displayed in the Functions app.",
        "ja_title": "関数定義の表記",
        "ja_desc": "関数グラフアプリで\n関数を f(x) と表記\nするか y= と表記\nするかを選択します"
    },
    {
        "id": "Set_SymbolDefaultFunction",
        "en_keys": ["Default"],
        "en_desc": "Default function notation.",
        "ja_title": "標準表記",
        "ja_desc": "標準の関数表記\nf(x) を使用します。"
    },
    {
        "id": "Set_SymbolArgFunction",
        "en_keys": ["Empty"],
        "en_desc": "Simplified function notation without argument.",
        "ja_title": "引数省略表記",
        "ja_desc": "引数を省略した\n簡潔な関数表記を\n使用します。"
    },
    {
        "id": "Set_SymbolArgDefaultFunction",
        "en_keys": ["Argument"],
        "en_desc": "Function notation with explicit variable argument (or complex argument).",
        "ja_title": "引数明示 / 偏角",
        "ja_desc": "【設定】変数引数を\n明示して関数を表示\n【複素数】複素数zの\n偏角arg(z)を計算"
    },
    {
        "id": "Set_BrightnessSettings",
        "en_keys": ["Brightness settings", "Brightness & sleep"],
        "en_desc": "Configure screen brightness and power-saving timeouts.",
        "ja_title": "明るさと省電力",
        "ja_desc": "画面輝度やスリープ\nまでの待機時間を\n設定します。"
    },
    {
        "id": "Set_Brightness",
        "en_keys": ["Brightness"],
        "en_desc": "Adjust backlight brightness level with Left/Right keys.",
        "ja_title": "画面の明るさ",
        "ja_desc": "左右キーで画面の\n明るさ設定値を調整\nします。"
    },
    {
        "id": "Set_DimAfter",
        "en_keys": ["Dim after (s)"],
        "en_desc": "Seconds of inactivity before dimming the display.",
        "ja_title": "自動減光(秒)",
        "ja_desc": "無操作時に画面を\n暗くするまでの秒数\nを設定します。"
    },
    {
        "id": "Set_SuspendAfter",
        "en_keys": ["Suspend after (s)"],
        "en_desc": "Seconds of inactivity before entering sleep mode.",
        "ja_title": "スリープ時間",
        "ja_desc": "無操作時に省電力\n休止に入るまでの\n秒数を設定します。"
    },
    {
        "id": "Set_ShortcutSteps",
        "en_keys": ["Shortcut steps"],
        "en_desc": "Number of brightness adjustment steps when using shortcuts.",
        "ja_title": "輝度調整段階",
        "ja_desc": "ショートカット操作\nでの輝度変化段階数\nを設定します。"
    },
    {
        "id": "Set_ClearShift",
        "en_keys": ["Shift + clear shortcut"],
        "en_desc": "Toggle Shift+Clear shortcut functionality.",
        "ja_title": "Shift+Clear機能",
        "ja_desc": "Shift+Clearキーの\nショートカット動作\nの有効/無効を切替\nえます。"
    },
    {
        "id": "Set_DateTime",
        "en_keys": ["Date/time", "Date and time"],
        "en_desc": "Set system date and clock time.",
        "ja_title": "日付と時刻",
        "ja_desc": "電卓内部の日付と\n時刻を設定します。"
    },
    {
        "id": "Set_ActivateClock",
        "en_keys": ["Activate clock", "Show clock"],
        "en_desc": "Toggle real-time clock.",
        "ja_title": "時計の有効化",
        "ja_desc": "内部クロックの動作\nを切り替えます。\n(Liliumでは上部バー\nの時計は非表示です)"
    },
    {
        "id": "Set_Date",
        "en_keys": ["Date"],
        "en_desc": "Set current year, month, and day.",
        "ja_title": "日付の設定",
        "ja_desc": "年月日を設定します"
    },
    {
        "id": "Set_Time",
        "en_keys": ["Time"],
        "en_desc": "Set current time or select time units in Toolbox.",
        "ja_title": "時間・時刻",
        "ja_desc": "【設定】現在時刻の\n時・分・秒を設定。\n【単位】秒(s)、分、\n時間(h)などの時間\n単位を入力します。"
    },
    {
        "id": "Set_FontSizes",
        "en_keys": ["Python font size"],
        "en_desc": "Choose between Large and Small font in the Python code editor.",
        "ja_title": "Python文字サイズ",
        "ja_desc": "Pythonエディタ内の\n文字サイズを大/小\nから選択します。\n・Large: 標準サイズ\n・Small: 小文字(広)"
    },
    {
        "id": "Set_LargeFont",
        "en_keys": ["Large"],
        "en_desc": "Large readable font in Python editor.",
        "ja_title": "大 (Large)",
        "ja_desc": "Python画面を読みや\nすい標準サイズの\n文字で表示します。"
    },
    {
        "id": "Set_SmallFont",
        "en_keys": ["Small"],
        "en_desc": "Small compact font in Python editor to show more code on screen.",
        "ja_title": "小 (Small)",
        "ja_desc": "Python画面の文字を\n小さくし、1画面に\n多くの行を表示しま\nす。"
    },
    {
        "id": "Set_SyntaxHighlighting",
        "en_keys": ["Syntax Highlighting"],
        "en_desc": "Toggle color syntax highlighting in Python scripts.",
        "ja_title": "構文強調表示",
        "ja_desc": "Pythonコードの予約\n語や文字列を色分け\n表示する機能のON/\nOFFを切り替えます。"
    },
    {
        "id": "Set_Language",
        "en_keys": ["Language", "English", "Français", "Español", "Deutsch", "Português", "Italiano", "Nederlands", "Magyar"],
        "en_desc": "Select calculator UI language.",
        "ja_title": "表示言語設定",
        "ja_desc": "電卓本体のメニュー\n言語を選択します。\n(Englishにしておく\nと右側に日本語解説\nが自動表示されます)"
    },
    {
        "id": "Set_Country",
        "en_keys": ["Country", "International", "United States", "United Kingdom", "France", "Germany", "Spain", "Italy", "Canada", "Netherlands", "Portugal"],
        "en_desc": "Select country mathematical conventions (quartile definitions, etc.).",
        "ja_title": "国・数学規約",
        "ja_desc": "国ごとの数学規約\n(四分位数の定義法\nなど)を選択します。\n通常はInternational\nまたはUSが標準です"
    },
    {
        "id": "Set_Accessibility",
        "en_keys": ["Accessibility"],
        "en_desc": "Screen color inversion, magnification, and gamma adjustment.",
        "ja_title": "視覚補助設定",
        "ja_desc": "画面の色反転(ダーク\n表示)やガンマ補正\nなどの視覚補助機能\nを設定します。"
    },
    {
        "id": "Set_InvertColors",
        "en_keys": ["Invert colors"],
        "en_desc": "Invert display colors (dark mode).",
        "ja_title": "色の反転表示",
        "ja_desc": "画面の白黒・色彩を\n反転してダーク表示\nに切り替えます。"
    },
    {
        "id": "Set_Magnify",
        "en_keys": ["Magnify"],
        "en_desc": "Toggle screen magnification.",
        "ja_title": "画面拡大表示",
        "ja_desc": "画面の一部を拡大\n表示する補助機能を\n切り替えます。"
    },
    {
        "id": "Set_Gamma",
        "en_keys": ["Gamma correction", "Red gamma", "Green gamma", "Blue gamma"],
        "en_desc": "Adjust display gamma levels.",
        "ja_title": "ガンマ補正",
        "ja_desc": "画面のRGBガンマ値\nを調整します。"
    },
    {
        "id": "Set_ExamMode",
        "en_keys": ["Exam mode", "Activate exam mode", "Reactivate exam mode", "Standard", "No sym", "No sym no text", "Dutch"],
        "en_desc": "Configure or activate examination mode (restricts memory/CAS features).",
        "ja_title": "試験モード",
        "ja_desc": "試験監督用の制限\nモードです。\n※有効化すると保存\nデータが消去される\nためご注意ください"
    },
    {
        "id": "Set_ExternalApps",
        "en_keys": ["External Apps", "External applications"],
        "en_desc": "Configure visibility and flash write permissions for external apps.",
        "ja_title": "外部アプリ設定",
        "ja_desc": "外部アドインアプリ\nの表示やメモリ書込\n権限を設定します。"
    },
    {
        "id": "Set_ExtAppShow",
        "en_keys": ["Show"],
        "en_desc": "Show external applications on the home screen.",
        "ja_title": "ホームに表示",
        "ja_desc": "ホーム画面に外部\nアプリを表示するか\nどうかを設定します"
    },
    {
        "id": "Set_ExtAppWrite",
        "en_keys": ["Write allowed"],
        "en_desc": "Allow external applications to write to persistent storage.",
        "ja_title": "書込権限の許可",
        "ja_desc": "外部アプリによる\nストレージへの保存\n書込を許可します。"
    },
    {
        "id": "Set_About",
        "en_keys": ["About"],
        "en_desc": "View system information, Lilium/Upsilon version, memory usage, and contributors.",
        "ja_title": "バージョン情報",
        "ja_desc": "Lilium / Upsilonの\nバージョンやメモリ\n使用量、開発者一覧\nを確認します。"
    },
    {
        "id": "Set_Username",
        "en_keys": ["Name", "Username"],
        "en_desc": "User name registered on this calculator.",
        "ja_title": "ユーザー名",
        "ja_desc": "本機に登録された\nユーザー名を表示・\n編集します。"
    },
    {
        "id": "Set_UpsilonLilium",
        "en_keys": ["Upsilon / Lilium", "Upsilon version"],
        "en_desc": "Upsilon OS & Lilium (SHARP Brain Edition) version.",
        "ja_title": "Lilium情報",
        "ja_desc": "Upsilonをベースに\nSHARP Brain向けに\n最適化移植された\n関数電卓「Lilium」\nのバージョンです。"
    },
    {
        "id": "Set_OmegaVersion",
        "en_keys": ["Omega version"],
        "en_desc": "Underlying Omega OS version.",
        "ja_title": "Omega版数",
        "ja_desc": "ベースシステムで\nあるOmegaの版数\n情報です。"
    },
    {
        "id": "Set_EpsilonVersion",
        "en_keys": ["Epsilon version", "Software version"],
        "en_desc": "NumWorks Epsilon base software version.",
        "ja_title": "Epsilon版数",
        "ja_desc": "NumWorks公式OS\n(Epsilon)のベース\nバージョン番号です"
    },
    {
        "id": "Set_MicroPythonVersion",
        "en_keys": ["µPython version", "MicroPython version"],
        "en_desc": "Embedded MicroPython interpreter version.",
        "ja_title": "Python版数",
        "ja_desc": "内蔵MicroPython\n処理系のバージョン\n番号です。"
    },
    {
        "id": "Set_Battery",
        "en_keys": ["Battery"],
        "en_desc": "Current battery charge status.",
        "ja_title": "バッテリー状態",
        "ja_desc": "現在のバッテリー\n残量状態を表示\nします。"
    },
    {
        "id": "Set_Memory",
        "en_keys": ["Memory", "Memory usage"],
        "en_desc": "View storage memory usage for variables and scripts.",
        "ja_title": "メモリ使用量",
        "ja_desc": "変数やスクリプトが\n使用しているメモリ\n容量を表示します。"
    },
    {
        "id": "Set_SerialNumber",
        "en_keys": ["Serial number"],
        "en_desc": "Hardware / simulator serial identifier.",
        "ja_title": "シリアル番号",
        "ja_desc": "デバイスの識別用\nシリアル番号です。"
    },
    {
        "id": "Set_FccId",
        "en_keys": ["FCC ID"],
        "en_desc": "Regulatory compliance ID.",
        "ja_title": "FCC認証番号",
        "ja_desc": "ハードウェアの無線\n・電磁適合認証ID\nです。"
    },
    {
        "id": "Set_Popups",
        "en_keys": ["Update pop-up", "Beta pop-up"],
        "en_desc": "Toggle startup update or beta notification pop-ups.",
        "ja_title": "通知ポップアップ",
        "ja_desc": "更新通知などの表示\n有無を切り替えます"
    },
    {
        "id": "Set_Contributors",
        "en_keys": ["Contributors"],
        "en_desc": "Open-source contributors to Upsilon and Omega.",
        "ja_title": "開発貢献者一覧",
        "ja_desc": "オープンソース開発\nに携わった貢献者の\n一覧を表示します。"
    },

    # =========================================================================
    # 3. 変数メニュー (F9: Variables)
    # =========================================================================
    {
        "id": "Var_Variables",
        "en_keys": ["Variables"],
        "en_desc": "Browse and insert user-defined variables, functions, sequences, lists, and matrices.",
        "ja_title": "変数一覧 (F9)",
        "ja_desc": "定義済みの数値変数\n関数、数列、リスト\n行列を呼び出します\n【Enter】で挿入\n【BackSpace】削除"
    },
    {
        "id": "Var_Expressions",
        "en_keys": ["Expressions"],
        "en_desc": "Stored numeric variables and algebraic expressions (e.g., 5 -> a).",
        "ja_title": "数値・式の変数",
        "ja_desc": "STO(→)で保存した\n数値や式の変数一覧\nです。\n【保存方法】\n計算画面で\n5→a のように入力"
    },

    # =========================================================================
    # 4. 関数カタログ (F1: Toolbox) - 微積分・基本関数
    # =========================================================================
    {
        "id": "Tb_Toolbox",
        "en_keys": ["Toolbox"],
        "en_desc": "Catalog of mathematical functions, calculus, matrices, probability, units, and constants.",
        "ja_title": "関数一覧 (F1)",
        "ja_desc": "あらゆる数学関数や\n微積分、行列、確率\n単位、物理定数を\n選んで数式に挿入\nできます。"
    },
    {
        "id": "Tb_Abs",
        "en_keys": ["abs(x)", "Absolute value"],
        "en_desc": "Absolute value |x| of a real number or modulus |z| of a complex number.",
        "ja_title": "絶対値 |x|",
        "ja_desc": "数値xの絶対値、\nまたは複素数zの\n大きさ|z|を求めます\n【書式】\nabs(x)\n【例】\nabs(-5) → 5"
    },
    {
        "id": "Tb_NthRoot",
        "en_keys": ["root(x,n)", "nth-root"],
        "en_desc": "The n-th root of x: x^(1/n).",
        "ja_title": "n乗根 √",
        "ja_desc": "xのn乗根(1/n乗)を\n計算します。\n【書式】\nroot(x,n)\n【例】\nroot(27,3) → 3"
    },
    {
        "id": "Tb_BasedLog",
        "en_keys": ["log(x,a)", "Logarithm to base a"],
        "en_desc": "Logarithm of x to base a: log_a(x).",
        "ja_title": "底aの対数 log",
        "ja_desc": "底をaとするxの対数\nlog_a(x)を求めます\n【書式】\nlog(真数x, 底a)\n【例】\nlog(8,2) → 3"
    },
    {
        "id": "Tb_Diff",
        "en_keys": ["diff(f(x),x,a)", "Derivative"],
        "en_desc": "Numerical derivative f'(a) of expression f(x) at x = a.",
        "ja_title": "微分係数 f'(a)",
        "ja_desc": "関数f(x)の x=a に\nおける微分係数を\n求めます。\n【書式】\ndiff(f(x),x,a)\n【例】\ndiff(x^2,x,3) → 6"
    },
    {
        "id": "Tb_Int",
        "en_keys": ["int(f(x),x,a,b)", "Integral"],
        "en_desc": "Definite integral of f(x) with respect to x from a to b.",
        "ja_title": "定積分 ∫f(x)dx",
        "ja_desc": "区間[a,b]における\nf(x)の定積分を計算\nします。\n【書式】\nint(f(x),x,a,b)\n【例】\nint(x^2,x,0,1)"
    },
    {
        "id": "Tb_Sum",
        "en_keys": ["sum(f(n),n,nmin,nmax)", "sum(f(n),n,a,b)", "Sum"],
        "en_desc": "Summation (Sigma) of f(n) for integer n from nmin to nmax.",
        "ja_title": "総和 Σ (シグマ)",
        "ja_desc": "n=開始値から終了値\nまでのf(n)の総和を\n計算します。\n【書式】\nsum(式,n,開始,終了)\n【例】\nsum(n,n,1,10) → 55"
    },
    {
        "id": "Tb_Product",
        "en_keys": ["product(f(n),n,nmin,nmax)", "prod(f(n),n,a,b)", "Product"],
        "en_desc": "Product (Pi) of f(n) for integer n from nmin to nmax.",
        "ja_title": "総乗 Π (積)",
        "ja_desc": "n=開始値から終了値\nまでのf(n)の総積を\n計算します。\n【書式】\nproduct(式,n,始,終)"
    },
    {
        "id": "Tb_Infinity",
        "en_keys": ["infinity", "inf", "Infinity"],
        "en_desc": "Infinity symbol (∞) for improper integrals or intervals.",
        "ja_title": "無限大 (∞)",
        "ja_desc": "広義積分や確率区間\nの端に使う無限大\n記号(∞)を入力しま\nす。"
    },

    # =========================================================================
    # 5. 関数カタログ - 複素数 (Complex Numbers)
    # =========================================================================
    {
        "id": "Tb_ComplexNumbers",
        "en_keys": ["Complex numbers"],
        "en_desc": "Complex number functions: modulus, argument, real part, imaginary part, conjugate.",
        "ja_title": "複素数の関数",
        "ja_desc": "複素数 z=a+bi の\n絶対値、偏角、実部\n虚部、共役複素数を\n計算するメニュー\nです。"
    },
    {
        "id": "Tb_Arg",
        "en_keys": ["arg(z)"],
        "en_desc": "Argument (phase angle) of complex number z.",
        "ja_title": "偏角 arg(z)",
        "ja_desc": "複素数zの偏角θを\n現在の角度単位で\n求めます。\n【書式】\narg(z)\n【例】\narg(1+i) → π/4"
    },
    {
        "id": "Tb_Re",
        "en_keys": ["re(z)", "Real part"],
        "en_desc": "Real part a of complex number z = a + bi.",
        "ja_title": "実部 Re(z)",
        "ja_desc": "複素数 z=a+bi の\n実部 a を返します。\n【書式】\nre(z)\n【例】\nre(3+4i) → 3"
    },
    {
        "id": "Tb_Im",
        "en_keys": ["im(z)", "Imaginary part"],
        "en_desc": "Imaginary part b of complex number z = a + bi.",
        "ja_title": "虚部 Im(z)",
        "ja_desc": "複素数 z=a+bi の\n虚部 b を返します。\n【書式】\nim(z)\n【例】\nim(3+4i) → 4"
    },
    {
        "id": "Tb_Conj",
        "en_keys": ["conj(z)", "Conjugate"],
        "en_desc": "Complex conjugate a - bi of z = a + bi.",
        "ja_title": "共役複素数 z*",
        "ja_desc": "複素数 z=a+bi の\n虚部の符号を反転\nした共役複素数\na-bi を返します。"
    },

    # =========================================================================
    # 6. 関数カタログ - 確率・組合せ・乱数・推定 (Probability & Combinatorics)
    # =========================================================================
    {
        "id": "Tb_Combinatorics",
        "en_keys": ["Combinatorics"],
        "en_desc": "Combinations nCr, permutations nPr, and factorial n!.",
        "ja_title": "順列・組合せ",
        "ja_desc": "場合の数で用いる\n組合せ(nCr)、順列\n(nPr)、階乗(n!)を\n計算します。"
    },
    {
        "id": "Tb_Binomial",
        "en_keys": ["binomial(n,k)", "Combination"],
        "en_desc": "Number of combinations (n choose k): n! / (k!(n-k)!).",
        "ja_title": "組合せ nCr",
        "ja_desc": "異なるn個からk個を\n選ぶ組合せの数nCk\nを計算します。\n【書式】\nbinomial(n,k)\n【例】\nbinomial(5,2)→10"
    },
    {
        "id": "Tb_Permute",
        "en_keys": ["permute(n,r)", "permute(n,k)", "Permutation"],
        "en_desc": "Number of permutations nPr: n! / (n-r)!.",
        "ja_title": "順列 nPr",
        "ja_desc": "異なるn個からr個を\n取り出して並べる\n順列の数nPrを計算\nします。\n【書式】\npermute(n,r)"
    },
    {
        "id": "Tb_Factorial",
        "en_keys": ["n!", "Factorial"],
        "en_desc": "Factorial of integer n: n! = 1 * 2 * ... * n.",
        "ja_title": "階乗 n!",
        "ja_desc": "1からnまでの自然数\nの積(階乗)を計算\nします。\n【例】\n5! → 120"
    },
    {
        "id": "Tb_NormalDist",
        "en_keys": ["Normal distribution", "Normal"],
        "en_desc": "Normal (Gaussian) probability distribution N(mu, sigma).",
        "ja_title": "正規分布 N(μ,σ)",
        "ja_desc": "平均μ、標準偏差σ\nの正規分布の確率\n密度・累積確率・\n逆確率を計算します"
    },
    {
        "id": "Tb_NormCDF",
        "en_keys": ["normcdf(a,μ,σ)", "P(X<a) where X follows N(μ,σ)"],
        "en_desc": "Cumulative normal probability P(X < a) for X ~ N(mu, sigma).",
        "ja_title": "正規分布の下側確率",
        "ja_desc": "正規分布N(μ,σ)に\nおいて X<a となる\n累積確率P(X<a)を\n求めます。\n【書式】\nnormcdf(a,μ,σ)"
    },
    {
        "id": "Tb_NormCDF2",
        "en_keys": ["normcdf2(a,b,μ,σ)", "normcdf(a,b,μ,σ)", "P(a<X<b) where X follows N(μ,σ)"],
        "en_desc": "Normal interval probability P(a < X < b) for X ~ N(mu, sigma).",
        "ja_title": "正規分布の区間確率",
        "ja_desc": "正規分布N(μ,σ)で\na<X<b となる区間\n確率を求めます。\n【書式】\nnormcdf2(a,b,μ,σ)"
    },
    {
        "id": "Tb_InvNorm",
        "en_keys": ["invnorm(a,μ,σ)", "invnorm(p,μ,σ)", "m where P(X<m)=a, X follows N(μ,σ)"],
        "en_desc": "Inverse normal distribution: value m such that P(X < m) = a.",
        "ja_title": "逆正規分布",
        "ja_desc": "正規分布N(μ,σ)で\n下側累積確率がaと\nなる境界値mを逆算\nします。\n【書式】\ninvnorm(確率a,μ,σ)"
    },
    {
        "id": "Tb_NormPDF",
        "en_keys": ["normpdf(x,μ,σ)", "Probability density of N(μ,σ)"],
        "en_desc": "Probability density function of normal distribution N(mu, sigma) at x.",
        "ja_title": "正規分布の確率密度",
        "ja_desc": "正規分布N(μ,σ)の\n位置xにおける確率\n密度関数の値を計算\nします。\n【書式】\nnormpdf(x,μ,σ)"
    },
    {
        "id": "Tb_BinomialDist",
        "en_keys": ["Binomial distribution", "Binomial"],
        "en_desc": "Discrete Binomial distribution B(n, p) with n trials and probability p.",
        "ja_title": "二項分布 B(n,p)",
        "ja_desc": "試行回数n、成功率p\nの二項分布の確率を\n計算します。"
    },
    {
        "id": "Tb_BinomPDF",
        "en_keys": ["binompdf(m,n,p)", "P(X=m) where X follows B(n,p)"],
        "en_desc": "Binomial probability P(X = m) for m successes in n trials with probability p.",
        "ja_title": "二項分布の確率",
        "ja_desc": "二項分布B(n,p)で\n成功回数がちょうど\nm回になる確率P(X=m)\nを求めます。\n【書式】\nbinompdf(m,n,p)"
    },
    {
        "id": "Tb_BinomCDF",
        "en_keys": ["binomcdf(m,n,p)", "P(X<=m) where X follows B(n,p)"],
        "en_desc": "Cumulative binomial probability P(X <= m) for X ~ B(n, p).",
        "ja_title": "二項分布の累積確率",
        "ja_desc": "二項分布B(n,p)で\n成功回数がm回以下\nとなる確率P(X≦m)\nを求めます。\n【書式】\nbinomcdf(m,n,p)"
    },
    {
        "id": "Tb_InvBinom",
        "en_keys": ["invbinom(a,n,p)", "invbinom(α,n,p)", "m where P(X<=m)=a, X follows B(n,p)"],
        "en_desc": "Inverse binomial: smallest m such that P(X <= m) >= a.",
        "ja_title": "逆二項分布",
        "ja_desc": "二項分布B(n,p)で\n累積確率がa以上と\nなる最小の回数mを\n求めます。\n【書式】\ninvbinom(確率a,n,p)"
    },
    {
        "id": "Tb_RandomMenu",
        "en_keys": ["Random and approximation", "Random & rounding"],
        "en_desc": "Generate random floats or random integers.",
        "ja_title": "乱数の生成",
        "ja_desc": "0以上1未満の実数\n乱数や、指定範囲の\n整数乱数を生成しま\nす。"
    },
    {
        "id": "Tb_RandomFloat",
        "en_keys": ["random()", "Floating point number in [0,1["],
        "en_desc": "Uniform random floating-point number in [0, 1).",
        "ja_title": "実数乱数 [0,1)",
        "ja_desc": "0以上1未満の一様な\n実数乱数を生成しま\nす。\n【書式】\nrandom()"
    },
    {
        "id": "Tb_Randint",
        "en_keys": ["randint(a,b)", "Random integer in [a,b]"],
        "en_desc": "Uniform random integer between a and b inclusive.",
        "ja_title": "整数乱数 [a,b]",
        "ja_desc": "a以上b以下の整数\nからランダムに1つ\n選びます。\n【書式】\nrandint(a,b)\n【例】\nrandint(1,6)"
    },
    {
        "id": "Tb_Fluctuation",
        "en_keys": ["Prediction Interval", "Fluctuation & prediction"],
        "en_desc": "Prediction intervals and confidence intervals for proportions.",
        "ja_title": "予測・信頼区間",
        "ja_desc": "母比率pや標本比率f\nと標本数nから95%\n予測区間や信頼区間\nを計算します。"
    },
    {
        "id": "Tb_Prediction95",
        "en_keys": ["prediction95(p,n)", "Prediction interval 95%"],
        "en_desc": "95% prediction interval [p - 1.96*sqrt(p(1-p)/n), p + 1.96*sqrt(p(1-p)/n)].",
        "ja_title": "95%予測区間",
        "ja_desc": "母比率p、標本数nに\n対する95%予測区間\nを計算します。\n【書式】\nprediction95(p,n)"
    },
    {
        "id": "Tb_Prediction",
        "en_keys": ["prediction(p,n)", "Simple prediction interval"],
        "en_desc": "Simple prediction interval [p - 1/sqrt(n), p + 1/sqrt(n)].",
        "ja_title": "簡易予測区間",
        "ja_desc": "母比率p、標本数nの\n簡易予測区間\n[p±1/√n] を計算\nします。\n【書式】\nprediction(p,n)"
    },
    {
        "id": "Tb_Confidence",
        "en_keys": ["confidence(f,n)", "Confidence interval"],
        "en_desc": "95% confidence interval [f - 1/sqrt(n), f + 1/sqrt(n)] for sample frequency f.",
        "ja_title": "信頼区間",
        "ja_desc": "標本比率f、標本数n\nから母比率の95%\n信頼区間を求めます\n【書式】\nconfidence(f,n)"
    },

    # =========================================================================
    # 7. 関数カタログ - 整数・端数処理 (Arithmetic)
    # =========================================================================
    {
        "id": "Tb_Arithmetic",
        "en_keys": ["Arithmetic"],
        "en_desc": "Number theory and rounding: GCD, LCM, prime factorization, quotient/remainder, floor, ceil, round.",
        "ja_title": "整数・端数処理",
        "ja_desc": "最大公約数、最小公\n倍数、素因数分解、\n割り算の商と余り、\n四捨五入や切り捨て\nなどを計算します。"
    },
    {
        "id": "Tb_GCD",
        "en_keys": ["gcd(p,q)", "gcd(a,b)", "GCD"],
        "en_desc": "Greatest common divisor of integers p and q.",
        "ja_title": "最大公約数 GCD",
        "ja_desc": "整数pとqの最大公約\n数を求めます。\n【書式】\ngcd(p,q)\n【例】\ngcd(24,36) → 12"
    },
    {
        "id": "Tb_LCM",
        "en_keys": ["lcm(p,q)", "lcm(a,b)", "LCM"],
        "en_desc": "Least common multiple of integers p and q.",
        "ja_title": "最小公倍数 LCM",
        "ja_desc": "整数pとqの最小公倍\n数を求めます。\n【書式】\nlcm(p,q)\n【例】\nlcm(12,18) → 36"
    },
    {
        "id": "Tb_Factor",
        "en_keys": ["factor(n)", "Integer factorization", "Prime factors"],
        "en_desc": "Prime factorization of integer n.",
        "ja_title": "素因数分解",
        "ja_desc": "整数nを素数の積に\n素因数分解します。\n【書式】\nfactor(n)\n【例】\nfactor(60)→2^2×3×5"
    },
    {
        "id": "Tb_Rem",
        "en_keys": ["rem(p,q)", "Remainder division p by q"],
        "en_desc": "Remainder of Euclidean division of p by q.",
        "ja_title": "割り算の余り",
        "ja_desc": "整数pをqで割った\nときの余り(剰余)を\n求めます。\n【書式】\nrem(p,q)\n【例】\nrem(17,5) → 2"
    },
    {
        "id": "Tb_Quo",
        "en_keys": ["quo(p,q)", "Quotient division p by q"],
        "en_desc": "Integer quotient of Euclidean division of p by q.",
        "ja_title": "割り算の商(整数)",
        "ja_desc": "整数pをqで割った\nときの整数の商を\n求めます。\n【書式】\nquo(p,q)\n【例】\nquo(17,5) → 3"
    },
    {
        "id": "Tb_Floor",
        "en_keys": ["floor(x)", "Floor"],
        "en_desc": "Greatest integer less than or equal to x (floor function).",
        "ja_title": "床関数(切り捨て)",
        "ja_desc": "x以下の最大の整数\n(ガウス記号[x])を\n返します。\n【書式】\nfloor(x)\n【例】\nfloor(3.8) → 3"
    },
    {
        "id": "Tb_Frac",
        "en_keys": ["frac(x)", "Fractional part"],
        "en_desc": "Fractional part of x: x - floor(x).",
        "ja_title": "小数部分 frac",
        "ja_desc": "実数xの小数部分\nx-floor(x) を取り\n出します。\n【書式】\nfrac(x)\n【例】\nfrac(3.14) → 0.14"
    },
    {
        "id": "Tb_Ceil",
        "en_keys": ["ceil(x)", "Ceiling"],
        "en_desc": "Smallest integer greater than or equal to x (ceiling function).",
        "ja_title": "天井関数(切上げ)",
        "ja_desc": "x以上の最小の整数\nを返します。\n【書式】\nceil(x)\n【例】\nceil(3.2) → 4"
    },
    {
        "id": "Tb_Round",
        "en_keys": ["round(x,n)", "Round to n digits"],
        "en_desc": "Round x to n decimal places.",
        "ja_title": "四捨五入 round",
        "ja_desc": "数値xを小数点以下\nn桁に四捨五入しま\nす。\n【書式】\nround(x,桁数n)\n【例】\nround(3.1415,2)"
    },

    # =========================================================================
    # 8. 関数カタログ - 行列・ベクトル・リスト (Matrices, Vectors, Lists)
    # =========================================================================
    {
        "id": "Tb_MatricesAndVectors",
        "en_keys": ["Matrices and vectors"],
        "en_desc": "Matrix operations (determinant, inverse, row echelon) and vector products.",
        "ja_title": "行列とベクトル",
        "ja_desc": "行列の生成、逆行列\n行列式、掃き出し法\nおよびベクトルの\n内積・外積・ノルム\nを計算します。"
    },
    {
        "id": "Tb_Matrices",
        "en_keys": ["Matrices"],
        "en_desc": "Matrix operations.",
        "ja_title": "行列の演算",
        "ja_desc": "行列の新規作成や\n逆行列、行列式、\n転置、階段形への\n変形を行います。"
    },
    {
        "id": "Tb_NewMatrix",
        "en_keys": ["[[1,2][3,4]]", "New matrix"],
        "en_desc": "Insert a 2D matrix template into the expression.",
        "ja_title": "新規行列の挿入",
        "ja_desc": "数式欄に行列の枠を\n挿入します。\n矢印キーでマス目を\n移動して数値を入力\nすると拡張されます"
    },
    {
        "id": "Tb_Inverse",
        "en_keys": ["inverse(M)", "Inverse"],
        "en_desc": "Inverse matrix M^(-1) of square matrix M.",
        "ja_title": "逆行列 M^-1",
        "ja_desc": "正方行列Mの逆行列\nを計算します。\n【書式】\ninverse(M)\n(M^-1 でも計算可)"
    },
    {
        "id": "Tb_Determinant",
        "en_keys": ["det(M)", "Determinant"],
        "en_desc": "Determinant det(M) of square matrix M.",
        "ja_title": "行列式 det(M)",
        "ja_desc": "正方行列Mの行列式\ndet(M)を求めます。\n【書式】\ndet(M)"
    },
    {
        "id": "Tb_Transpose",
        "en_keys": ["transpose(M)", "Transpose"],
        "en_desc": "Transpose of matrix M (swapping rows and columns).",
        "ja_title": "転置行列 ^tM",
        "ja_desc": "行列Mの行と列を\n入れ替えた転置行列\nを返します。\n【書式】\ntranspose(M)"
    },
    {
        "id": "Tb_Trace",
        "en_keys": ["trace(M)", "Trace"],
        "en_desc": "Trace of square matrix M (sum of diagonal elements).",
        "ja_title": "トレース(対角和)",
        "ja_desc": "正方行列Mの対角\n成分の和(トレース)\nを求めます。\n【書式】\ntrace(M)"
    },
    {
        "id": "Tb_Dim",
        "en_keys": ["dim(M)", "Size"],
        "en_desc": "Dimensions {rows, cols} of a matrix or length of a list.",
        "ja_title": "行列・リスト寸法",
        "ja_desc": "行列Mの{行数,列数}\nまたはリストの要素\n数を返します。\n【書式】\ndim(M)"
    },
    {
        "id": "Tb_REF",
        "en_keys": ["ref(M)", "Row echelon form"],
        "en_desc": "Row echelon form of matrix M by Gaussian elimination.",
        "ja_title": "行階段形 (REF)",
        "ja_desc": "ガウスの消去法で\n行列Mを行階段形に\n変形します。\n【書式】\nref(M)"
    },
    {
        "id": "Tb_RREF",
        "en_keys": ["rref(M)", "Reduced row echelon form"],
        "en_desc": "Reduced row echelon form (Gauss-Jordan elimination).",
        "ja_title": "簡約階段形(RREF)",
        "ja_desc": "掃き出し法で行列M\nを被約行階段形に\n変形します。連立\n方程式の解法に便利\n【書式】\nrref(M)"
    },
    {
        "id": "Tb_Identity",
        "en_keys": ["identity(n)", "Identity matrix of size n"],
        "en_desc": "n x n identity matrix with 1s on the diagonal.",
        "ja_title": "単位行列 I_n",
        "ja_desc": "n行n列の単位行列を\n生成します。\n【書式】\nidentity(n)"
    },
    {
        "id": "Tb_Vectors",
        "en_keys": ["Vectors"],
        "en_desc": "Vector operations: dot product, cross product, and Euclidean norm.",
        "ja_title": "ベクトル演算",
        "ja_desc": "ベクトルの内積、\n外積、ノルム(長さ)\nを計算します。"
    },
    {
        "id": "Tb_Dot",
        "en_keys": ["dot(u,v)", "Dot product"],
        "en_desc": "Scalar dot product u . v of two vectors.",
        "ja_title": "ベクトルの内積",
        "ja_desc": "2つのベクトルu,vの\n内積(スカラー積)を\n計算します。\n【書式】\ndot(u,v)"
    },
    {
        "id": "Tb_Cross",
        "en_keys": ["cross(u,v)", "Cross product"],
        "en_desc": "3D vector cross product u x v.",
        "ja_title": "ベクトルの外積",
        "ja_desc": "3次元ベクトルu,vの\n外積(ベクトル積)を\n計算します。\n【書式】\ncross(u,v)"
    },
    {
        "id": "Tb_Norm",
        "en_keys": ["norm(u)", "Norm"],
        "en_desc": "Euclidean norm ||u|| (length) of vector u.",
        "ja_title": "ベクトル大きさ",
        "ja_desc": "ベクトルuの長さ\n(ユークリッドノルム)\n||u||を計算します。\n【書式】\nnorm(u)"
    },
    {
        "id": "Tb_Lists",
        "en_keys": ["List", "Lists"],
        "en_desc": "List operations: sort ascending/descending, minimum, and maximum.",
        "ja_title": "リスト操作",
        "ja_desc": "リスト(配列)の昇順\n・降順の並べ替えや\n最大値・最小値の\n取得を行います。"
    },
    {
        "id": "Tb_SortAsc",
        "en_keys": ["sort<(L)", "Sort ascending"],
        "en_desc": "Sort list L in ascending order.",
        "ja_title": "昇順ソート(小→大)",
        "ja_desc": "リストLの要素を\n小さい順(昇順)に\n並べ替えます。\n【書式】\nsort<(L)"
    },
    {
        "id": "Tb_SortDesc",
        "en_keys": ["sort>(L)", "Sort descending"],
        "en_desc": "Sort list L in descending order.",
        "ja_title": "降順ソート(大→小)",
        "ja_desc": "リストLの要素を\n大きい順(降順)に\n並べ替えます。\n【書式】\nsort>(L)"
    },
    {
        "id": "Tb_Max",
        "en_keys": ["max(L)", "Maximum"],
        "en_desc": "Maximum element in list L (or local maximum of a function).",
        "ja_title": "最大値 / 極大値",
        "ja_desc": "【リスト】max(L)で\n最大要素を取得。\n【グラフ/統計】\n関数の極大値または\nデータの最大値。"
    },
    {
        "id": "Tb_Min",
        "en_keys": ["min(L)", "Minimum"],
        "en_desc": "Minimum element in list L (or local minimum of a function).",
        "ja_title": "最小値 / 極小値",
        "ja_desc": "【リスト】min(L)で\n最小要素を取得。\n【グラフ/統計】\n関数の極小値または\nデータの最小値。"
    },

    # =========================================================================
    # 9. 関数カタログ - 双曲線関数 (Hyperbolic Trigonometry)
    # =========================================================================
    {
        "id": "Tb_Hyperbolic",
        "en_keys": ["Hyperbolic trigonometry"],
        "en_desc": "Hyperbolic sine, cosine, tangent and their inverses.",
        "ja_title": "双曲線関数",
        "ja_desc": "双曲線関数\nsinh, cosh, tanh\nとその逆双曲線関数\nを計算します。"
    },
    {
        "id": "Tb_Sinh",
        "en_keys": ["sinh(x)", "Hyperbolic sine"],
        "en_desc": "Hyperbolic sine: (e^x - e^(-x)) / 2.",
        "ja_title": "双曲線正弦 sinh",
        "ja_desc": "双曲線正弦\n(e^x - e^-x)/2\nを計算します。\n【書式】\nsinh(x)"
    },
    {
        "id": "Tb_Cosh",
        "en_keys": ["cosh(x)", "Hyperbolic cosine"],
        "en_desc": "Hyperbolic cosine: (e^x + e^(-x)) / 2.",
        "ja_title": "双曲線余弦 cosh",
        "ja_desc": "双曲線余弦(カテナリ)\n(e^x + e^-x)/2\nを計算します。\n【書式】\ncosh(x)"
    },
    {
        "id": "Tb_Tanh",
        "en_keys": ["tanh(x)", "Hyperbolic tangent"],
        "en_desc": "Hyperbolic tangent: sinh(x) / cosh(x).",
        "ja_title": "双曲線正接 tanh",
        "ja_desc": "双曲線正接\nsinh(x)/cosh(x)\nを計算します。\n【書式】\ntanh(x)"
    },
    {
        "id": "Tb_Asinh",
        "en_keys": ["asinh(x)", "Inverse hyperbolic sine"],
        "en_desc": "Inverse hyperbolic sine.",
        "ja_title": "逆双曲線正弦",
        "ja_desc": "sinh(x)の逆関数\nasinh(x)を計算しま\nす。"
    },
    {
        "id": "Tb_Acosh",
        "en_keys": ["acosh(x)", "Inverse hyperbolic cosine"],
        "en_desc": "Inverse hyperbolic cosine (for x >= 1).",
        "ja_title": "逆双曲線余弦",
        "ja_desc": "cosh(x)の逆関数\nacosh(x)を計算しま\nす(x≧1)。"
    },
    {
        "id": "Tb_Atanh",
        "en_keys": ["atanh(x)", "Inverse hyperbolic tangent"],
        "en_desc": "Inverse hyperbolic tangent (for |x| < 1).",
        "ja_title": "逆双曲線正接",
        "ja_desc": "tanh(x)の逆関数\natanh(x)を計算しま\nす(|x|<1)。"
    },

    # =========================================================================
    # 10. 関数カタログ - 論理・ビット演算 (Logic & Bitwise Operations)
    # =========================================================================
    {
        "id": "Tb_Logic",
        "en_keys": ["Logic", "Logic and bits"],
        "en_desc": "Bitwise logic operations: AND, OR, XOR, NOT, bit shifts, rotations, and bit manipulation.",
        "ja_title": "論理・ビット演算",
        "ja_desc": "2進ビット演算(AND,\nOR,XOR,NOT)、シフト\n回転、特定ビットの\n操作を行います。"
    },
    {
        "id": "Tb_ExplicitBits",
        "en_keys": ["Explicit number of bits", "Explicit bit size"],
        "en_desc": "Bitwise operations with an explicit bit width n.",
        "ja_title": "ビット幅指定演算",
        "ja_desc": "ビット数nを明示し\nて反転・シフト・\n回転を行います。"
    },
    {
        "id": "Tb_LogicAnd",
        "en_keys": ["and(a,b)", "a AND b"],
        "en_desc": "Bitwise AND of integers a and b.",
        "ja_title": "ビット論理積 AND",
        "ja_desc": "整数aとbの各ビット\nの論理積(AND)を\n計算します。\n【書式】\nand(a,b)"
    },
    {
        "id": "Tb_LogicOr",
        "en_keys": ["or(a,b)", "a OR b"],
        "en_desc": "Bitwise OR of integers a and b.",
        "ja_title": "ビット論理和 OR",
        "ja_desc": "整数aとbの各ビット\nの論理和(OR)を\n計算します。\n【書式】\nor(a,b)"
    },
    {
        "id": "Tb_LogicXor",
        "en_keys": ["xor(a,r)", "a XOR b"],
        "en_desc": "Bitwise exclusive OR (XOR) of integers a and b.",
        "ja_title": "排他的論理和 XOR",
        "ja_desc": "整数aとbの各ビット\nの排他的論理和(XOR)\nを計算します。\n【書式】\nxor(a,b)"
    },
    {
        "id": "Tb_LogicNot",
        "en_keys": ["not(a)", "not(a,n)", "NOT a"],
        "en_desc": "Bitwise NOT (1's complement) of integer a.",
        "ja_title": "ビット反転 NOT",
        "ja_desc": "整数aの各ビットを\n反転します。\n【書式】\nnot(a) / not(a,n)"
    },
    {
        "id": "Tb_LogicSll",
        "en_keys": ["sll(a,s)", "Logical shift left [a << s]"],
        "en_desc": "Logical left shift of a by s bits (a << s).",
        "ja_title": "左論理シフト <<",
        "ja_desc": "aを左へsビット論理\nシフト(a×2^s)しま\nす。\n【書式】\nsll(a,s)"
    },
    {
        "id": "Tb_LogicSrl",
        "en_keys": ["srl(a,s)", "Logical shift right [a >> s]"],
        "en_desc": "Logical right shift of a by s bits (zero-fill).",
        "ja_title": "右論理シフト >>",
        "ja_desc": "aを右へsビット論理\nシフトします。\n【書式】\nsrl(a,s)"
    },
    {
        "id": "Tb_LogicSra",
        "en_keys": ["sra(a,s)", "sra(a,s,n)", "Arithmetic shift right [a >>> s]"],
        "en_desc": "Arithmetic right shift preserving the sign bit.",
        "ja_title": "右算術シフト >>>",
        "ja_desc": "符号ビットを保った\nままaを右へsビット\n算術シフトします。\n【書式】\nsra(a,s)"
    },
    {
        "id": "Tb_LogicRol",
        "en_keys": ["rol(a,r)", "rol(a,r,n)", "Rotate r bits of a to the left"],
        "en_desc": "Rotate bits of a to the left by r positions.",
        "ja_title": "左ビット回転 ROL",
        "ja_desc": "aのビット列を左へ\nrビット巡回シフト\nします。\n【書式】\nrol(a,r)"
    },
    {
        "id": "Tb_LogicRor",
        "en_keys": ["ror(a,r)", "ror(a,r,n)", "Rotate r bits of a to the right"],
        "en_desc": "Rotate bits of a to the right by r positions.",
        "ja_title": "右ビット回転 ROR",
        "ja_desc": "aのビット列を右へ\nrビット巡回シフト\nします。\n【書式】\nror(a,r)"
    },
    {
        "id": "Tb_LogicBitGet",
        "en_keys": ["bit(a,b)", "Get bit b in a"],
        "en_desc": "Extract the b-th bit (0 or 1) of integer a.",
        "ja_title": "ビット取得 bit",
        "ja_desc": "整数aの下位b番目\n(0始まり)のビット値\n(0/1)を取得します。\n【書式】\nbit(a,b)"
    },
    {
        "id": "Tb_LogicBitSet",
        "en_keys": ["bset(a,b)", "Set bit b in a"],
        "en_desc": "Set the b-th bit of integer a to 1.",
        "ja_title": "ビット1設定 bset",
        "ja_desc": "整数aの第bビットを\n1にセットします。\n【書式】\nbset(a,b)"
    },
    {
        "id": "Tb_LogicBitClr",
        "en_keys": ["bclr(a,b)", "Clear bit b in a"],
        "en_desc": "Clear the b-th bit of integer a to 0.",
        "ja_title": "ビット0消去 bclr",
        "ja_desc": "整数aの第bビットを\n0にクリアします。\n【書式】\nbclr(a,b)"
    },
    {
        "id": "Tb_LogicBitFlp",
        "en_keys": ["bflp(a,b)", "Flip bit b in a"],
        "en_desc": "Toggle (flip) the b-th bit of integer a.",
        "ja_title": "ビット反転 bflp",
        "ja_desc": "整数aの第bビットを\n反転(0↔1)します。\n【書式】\nbflp(a,b)"
    },
    {
        "id": "Tb_LogicBic",
        "en_keys": ["bic(a,b)", "bic(a,b,n)", "Clear a with b [a AND NOT b]"],
        "en_desc": "Bit clear with mask: a AND (NOT b).",
        "ja_title": "マスク消去 bic",
        "ja_desc": "ビットマスクbで指定\nしたビットを消去\n(a AND NOT b)します\n【書式】\nbic(a,b)"
    },
    {
        "id": "Tb_LogicTc",
        "en_keys": ["tc(a,n)", "Two's complement equivalent"],
        "en_desc": "Two's complement representation of a on n bits.",
        "ja_title": "2の補数表現 tc",
        "ja_desc": "整数aをnビット幅の\n2の補数表現に変換\nします。\n【書式】\ntc(a,n)"
    },
    {
        "id": "Tb_LogicClog2",
        "en_keys": ["clog2(a)", "Number of bits needed to store a"],
        "en_desc": "Number of bits required to represent integer a.",
        "ja_title": "必要ビット幅",
        "ja_desc": "整数aを表現するのに\n必要なビット数を\n返します。\n【書式】\nclog2(a)"
    },

    # =========================================================================
    # 11. 関数カタログ - 単位・物理定数・化学 (Units, Physics, Chemistry)
    # =========================================================================
    {
        "id": "Tb_Units",
        "en_keys": ["Units", "Units and constants", "Metric", "Imperial"],
        "en_desc": "Measurement units (distance, mass, time, energy, electricity, etc.) for unit conversions.",
        "ja_title": "単位の入力と換算",
        "ja_desc": "長さ・質量・時間・\nエネルギー・電気等\nの単位を入力します\n【単位換算の例】\n5_km→_m"
    },
    {
        "id": "Tb_UnitTimeItems",
        "en_keys": ["Second", "Millisecond", "Microsecond", "Nanosecond", "Minute", "Hour", "Day", "Week", "Month", "Year"],
        "en_desc": "Time units: s, ms, us, ns, min, h, day, week, month, year.",
        "ja_title": "時間の単位",
        "ja_desc": "秒(_s)、ミリ秒(ms)\nマイクロ秒(μs)、分\n(_min)、時間(_h)、\n日・週・月・年を\n入力します。"
    },
    {
        "id": "Tb_UnitDistance",
        "en_keys": [
            "Distance", "Length", "Distances", "Radii",
            "Kilometer", "Meter", "Centimeter", "Millimeter", "Micrometer", "Nanometer", "Picometer",
            "Inch", "Foot", "Yard", "Mile", "Astronomical unit", "Light year", "Parsec"
        ],
        "en_desc": "Distance and length units (m, km, cm, mm, inch, ft, yd, mi, au, ly, pc).",
        "ja_title": "長さ・距離の単位",
        "ja_desc": "メートル(m,km,cm,\nmm,nm)、インチ(in)\nフィート(ft)、マイル\n天文単位(au)、光年\nなどを入力します。"
    },
    {
        "id": "Tb_UnitMass",
        "en_keys": [
            "Mass", "Kilogram", "Gram", "Milligram", "Microgram",
            "Metric Ton", "Ounce", "Pound", "Short Ton", "Long Ton"
        ],
        "en_desc": "Mass units (kg, g, mg, ug, t, oz, lb) or astronomical/particle masses.",
        "ja_title": "質量の単位",
        "ja_desc": "キログラム(_kg)、\nグラム(_g,_mg)、\nトン(_t)、オンス\n(_oz)、ポンド(_lb)\nを入力します。"
    },
    {
        "id": "Tb_UnitCurrent",
        "en_keys": ["Electric current", "Ampere", "Milliampere", "Microampere"],
        "en_desc": "Electric current units: Ampere (A, mA, uA).",
        "ja_title": "電流の単位 (A)",
        "ja_desc": "アンペア(_A)、\nミリアンペア(_mA)、\nマイクロアンペア\n(_μA)を入力します"
    },
    {
        "id": "Tb_UnitTemperature",
        "en_keys": ["Temperature", "Kelvin", "Celsius", "Fahrenheit"],
        "en_desc": "Temperature units: Kelvin (K), Celsius (°C), Fahrenheit (°F).",
        "ja_title": "温度の単位",
        "ja_desc": "絶対温度ケルビン\n(_K)、摂氏(_°C)、\n華氏(_°F)を入力・\n換算します。"
    },
    {
        "id": "Tb_UnitAmount",
        "en_keys": ["Quantity of matter", "Mole", "Millimole", "Micromole"],
        "en_desc": "Amount of substance units: mole (mol, mmol, umol).",
        "ja_title": "物質量の単位(mol)",
        "ja_desc": "モル(_mol)、\nミリモル(_mmol)、\nマイクロモル(_μmol)\nを入力します。"
    },
    {
        "id": "Tb_UnitLight",
        "en_keys": ["Luminous intensity", "Candela", "Luminous Flux", "Lumen", "Illuminance", "Lux", "Solid Angle", "Steradian"],
        "en_desc": "Optical and solid angle units: candela (cd), lumen (lm), lux (lx), steradian (sr).",
        "ja_title": "光度・光束・立体角",
        "ja_desc": "カンデラ(cd)、ルー\nメン(lm)、ルクス\n(lx)、ステラジアン\n(sr)を入力します。"
    },
    {
        "id": "Tb_UnitFrequency",
        "en_keys": ["Frequency", "Hertz", "Kilohertz", "Megahertz", "Gigahertz"],
        "en_desc": "Frequency units: Hertz (Hz, kHz, MHz, GHz).",
        "ja_title": "周波数の単位(Hz)",
        "ja_desc": "ヘルツ(_Hz, _kHz,\n_MHz, _GHz)を入力\nします。"
    },
    {
        "id": "Tb_UnitForce",
        "en_keys": ["Force", "Strength", "Newton", "Kilonewton", "Millinewton"],
        "en_desc": "Force units: Newton (N, kN, mN).",
        "ja_title": "力の単位 (N)",
        "ja_desc": "ニュートン(_N,\n_kN, _mN)を入力\nします。"
    },
    {
        "id": "Tb_UnitPressure",
        "en_keys": ["Pressure", "Pascal", "Hectopascal", "Bar", "Atmosphere"],
        "en_desc": "Pressure units: Pascal (Pa, hPa), Bar (bar), Atmosphere (atm).",
        "ja_title": "圧力の単位 (Pa)",
        "ja_desc": "パスカル(_Pa)、\nヘクトパスカル(hPa)\nバール(bar)、標準\n気圧(atm)を入力。"
    },
    {
        "id": "Tb_UnitEnergy",
        "en_keys": ["Energy", "Joule", "Kilojoule", "Millijoule", "Electronvolt", "Kiloelectronvolt", "Megaelectronvolt", "Millielectronvolt"],
        "en_desc": "Energy units: Joule (J, kJ, mJ) and Electronvolt (eV, keV, MeV, meV).",
        "ja_title": "エネルギーの単位",
        "ja_desc": "ジュール(_J,_kJ)や\n電子ボルト(_eV,\n_keV,_MeV)を入力\nします。"
    },
    {
        "id": "Tb_UnitPower",
        "en_keys": ["Power", "Watt", "Kilowatt", "Megawatt", "Gigawatt", "Milliwatt", "Microwatt"],
        "en_desc": "Power units: Watt (W, mW, uW, kW, MW, GW) or Power regression.",
        "ja_title": "仕事率(W)/累乗",
        "ja_desc": "【単位】ワット(_W,\n_kW,_MW,_mW)を入力\n【回帰】累乗回帰\ny=a·x^b を選択。"
    },
    {
        "id": "Tb_UnitElectricity",
        "en_keys": [
            "Electric charge", "Electrical charge", "Coulomb",
            "Electric potential", "Volt", "Kilovolt", "Millivolt", "Microvolt",
            "Electrical capacitance", "Electrical capacity", "Farad", "Millifarad", "Microfarad",
            "Electrical resistance", "Ohm", "Kiloohm",
            "Electrical conductance", "Siemens", "Millisiemens",
            "Magnetic field", "Tesla",
            "Electrical inductance", "Inductance", "Henry"
        ],
        "en_desc": "Electromagnetic units: Coulomb (C), Volt (V), Farad (F), Ohm, Siemens (S), Tesla (T), Henry (H).",
        "ja_title": "電磁気の単位",
        "ja_desc": "電荷(C)、電圧(V)、\n静電容量(F)、電気\n抵抗(Ω)、コンダク\nタンス(S)、磁束密度\n(T)、インダクタンス\n(H)を入力します。"
    },
    {
        "id": "Tb_UnitAreaVolume",
        "en_keys": [
            "Area", "Square meter", "Hectare", "Acre",
            "Volume", "Liter", "Deciliter", "Centiliter", "Milliliter",
            "Teaspoon", "Tablespoon", "Fluid ounce", "Cup", "Pint", "Quart", "Gallon"
        ],
        "en_desc": "Area (m^2, ha, acre) and Volume (L, dL, cL, mL, gal, pt, qt, cup, floz) units.",
        "ja_title": "面積・体積の単位",
        "ja_desc": "面積(m^2,ha,acre)\nおよび体積・容積\n(L,dL,mL,ガロン等)\nの単位を入力します"
    },
    {
        "id": "Tb_Physics",
        "en_keys": ["Physics", "Fundamental Constants", "Nuclear and Atomic Constants"],
        "en_desc": "Fundamental physical constants (c, h, G, e, k_B, N_A, R, etc.).",
        "ja_title": "物理定数カタログ",
        "ja_desc": "光速c、プランク定数\n万有引力定数G、\n素電荷e、アボガドロ\n定数、気体定数など\nの物理定数一覧です"
    },
    {
        "id": "Tb_PhysSpeedOfLight",
        "en_keys": ["Speed of light"],
        "en_desc": "Speed of light in vacuum c = 2.99792458 * 10^8 m/s.",
        "ja_title": "真空中の光速 c",
        "ja_desc": "真空中の光の速さ\n2.99792458×10^8\n[m/s] を入力します"
    },
    {
        "id": "Tb_PhysSpeed",
        "en_keys": ["Speed", "Speed of sound", "Sea level, 20°C", "In water", "In steel", "In glass", "Escape Velocity", "Of Earth", "Of Moon", "Of Sun"],
        "en_desc": "Speed of sound in media and planetary escape velocities.",
        "ja_title": "音速・脱出速度",
        "ja_desc": "空気・水・金属中の\n音速、および地球・\n月・太陽の第二宇宙\n速度(脱出速度)です"
    },
    {
        "id": "Tb_PhysBoltzmann",
        "en_keys": ["Boltzmann Constant"],
        "en_desc": "Boltzmann constant k_B = 1.380649 * 10^-23 J/K.",
        "ja_title": "ボルツマン定数",
        "ja_desc": "ボルツマン定数 k_B\n1.380649×10^-23\n[J/K] を入力します"
    },
    {
        "id": "Tb_PhysAvogadro",
        "en_keys": ["Avogadro Constant"],
        "en_desc": "Avogadro constant N_A = 6.02214076 * 10^23 mol^-1.",
        "ja_title": "アボガドロ定数",
        "ja_desc": "アボガドロ定数 N_A\n6.02214076×10^23\n[/mol]を入力します"
    },
    {
        "id": "Tb_PhysGas",
        "en_keys": ["Gas Constant"],
        "en_desc": "Molar gas constant R = 8.314462618 J/(mol*K).",
        "ja_title": "気体定数 R",
        "ja_desc": "モル気体定数 R\n8.3144626 [J/mol·K]\nを入力します。"
    },
    {
        "id": "Tb_PhysThermo",
        "en_keys": ["Thermodynamics", "Stefan-Boltzmann Constant", "Water Triple Point", "Wien Constant", "Atmospheric Pressure"],
        "en_desc": "Thermodynamic constants: Stefan-Boltzmann, water triple point, Wien, 1 atm (101325 Pa).",
        "ja_title": "熱力学の物理定数",
        "ja_desc": "ステファン・ボルツ\nマン定数、水の三重点\nウィーン変位則定数\n標準大気圧(101325Pa)\nを入力します。"
    },
    {
        "id": "Tb_PhysElemCharge",
        "en_keys": ["Elementary Charge"],
        "en_desc": "Elementary electric charge e = 1.602176634 * 10^-19 C.",
        "ja_title": "電気素量 (素電荷) e",
        "ja_desc": "陽子・電子の素電荷\n1.6021766×10^-19 C\nを入力します。"
    },
    {
        "id": "Tb_PhysElectro",
        "en_keys": [
            "Electromagnetism", "Coulomb Constant",
            "Vacuum permittivity", "Vacuum permeability", "Vacuum Impedance",
            "Faraday Constant", "Conductivity constants", "Electricity", "Resistivity Constants",
            "Sea Water", "Water", "Air", "Glass", "Wood"
        ],
        "en_desc": "Electromagnetic constants: epsilon_0, mu_0, Coulomb k_e, Faraday F, resistivity/conductivity.",
        "ja_title": "電磁気の物理定数",
        "ja_desc": "クーロン定数、真空\nの誘電率ε0・透磁率\nμ0、ファラデー定数\n各物質の伝導率・\n抵抗率を入力します"
    },
    {
        "id": "Tb_PhysGravityAstro",
        "en_keys": ["Gravitation", "Acceleration", "Constant", "Moon", "Earth", "Sun", "Earth - Moon", "Earth - Sun"],
        "en_desc": "Standard gravity g = 9.80665 m/s^2, gravitational constant G, and astronomical masses/radii/distances.",
        "ja_title": "重力・天文定数",
        "ja_desc": "標準重力加速度\ng=9.80665 m/s^2、\n万有引力定数G、\n太陽・地球・月の\n質量と半径です。"
    },
    {
        "id": "Tb_PhysPlanck",
        "en_keys": ["Planck Constant", "Reduced Planck Constant"],
        "en_desc": "Planck constant h = 6.62607015 * 10^-34 J*s and h-bar = h / (2*pi).",
        "ja_title": "プランク定数 h / ħ",
        "ja_desc": "プランク定数 h\n6.62607015×10^-34\n[J·s] またはディラ\nック定数ħ=h/2πです"
    },
    {
        "id": "Tb_PhysQuantum",
        "en_keys": [
            "Particles Mass",
            "Electron", "Proton", "Neutron", "Muon", "Tauon", "W Boson", "Z Boson",
            "Atomic Mass Unit", "Bohr Radius", "Bohr Magneton", "Nuclear Magneton",
            "Fine Structure Constant", "Rydberg Constant", "Hartree Constant",
            "Magnetic Flux Quantum", "Conductance Quantum", "Circulation Quantum",
            "Planck Units", "Planck Mass", "Planck Length", "Planck Time", "Planck Temperature",
            "Planck Charge", "Planck Force", "Planck Energy", "Planck Power",
            "Planck Density", "Planck Quantity of Movement", "Planck Linear Mass",
            "Planck Tension", "Planck Current", "Planck Pressure", "Planck Impedance",
            "Ultimate Answer"
        ],
        "en_desc": "Quantum, atomic, and particle constants (electron/proton/neutron mass, Bohr radius, fine structure alpha).",
        "ja_title": "素粒子・原子核定数",
        "ja_desc": "電子・陽子・中性子\n等の質量、原子質量\n単位u、ボーア半径\n微細構造定数α、\nプランク単位系です"
    },
    {
        "id": "Tb_Chemistry",
        "en_keys": ["Chemistry", "Molar masses (1,2...)", "Molar masses (A,B...)", "pKa"],
        "en_desc": "Chemical element molar masses (g/mol) and acid dissociation constants (pKa).",
        "ja_title": "化学・モル質量",
        "ja_desc": "全元素のモル質量\n(原子量 g/mol)を\n原子番号順・ABC順\nで呼び出せます。\n酸解離定数pKaも収録"
    },

    # =========================================================================
    # 12. 計算アプリの追加結果 (Calculation Additional Results)
    # =========================================================================
    {
        "id": "Calc_AdditionalResults",
        "en_keys": ["Additional results"],
        "en_desc": "View additional representations of the calculation result (hex/bin, prime factors, trig form, etc.).",
        "ja_title": "追加の計算結果",
        "ja_desc": "計算結果に対する\n別形式の表現(16進\n2進、帯分数、極形式\n行列特性など)を\n一覧表示します。"
    },
    {
        "id": "Calc_HexBin",
        "en_keys": ["Hexadecimal", "Binary"],
        "en_desc": "Hexadecimal (0x...) and Binary (0b...) integer representations.",
        "ja_title": "16進数・2進数表記",
        "ja_desc": "整数を16進数(0x)\nまたは2進数(0b)で\n表した値です。"
    },
    {
        "id": "Calc_MixedEuclidean",
        "en_keys": ["Mixed fraction", "Euclidean division"],
        "en_desc": "Mixed fraction (integer + proper fraction) and Euclidean division (p = b*q + r).",
        "ja_title": "帯分数・整数の除算",
        "ja_desc": "仮分数を整数+真分数\nの帯分数で表した形\nおよび商と余りによる\n除算等式です。"
    },
    {
        "id": "Calc_PolyForms",
        "en_keys": ["Canonical form", "Factorized form", "Discriminant", "Root", "First root", "Second root"],
        "en_desc": "Quadratic polynomial analysis: canonical (vertex) form, factored form, discriminant Delta, and roots.",
        "ja_title": "2次式の解析結果",
        "ja_desc": "2次式の平方完成形\n(標準形)、因数分解\n判別式 Δ=b^2-4ac、\nおよび実数解です。"
    },

    # =========================================================================
    # 13. 各アプリ共通タブ・グラフ・テーブル操作 (Tabs, Graph & Table Controls)
    # =========================================================================
    {
        "id": "Tab_Graph",
        "en_keys": ["Graph", "Plot graph"],
        "en_desc": "Switch to the Graph tab to plot curves, trace points, zoom, and analyze functions.",
        "ja_title": "グラフ表示タブ",
        "ja_desc": "定義した関数や数列\nデータのグラフを\n描画・解析します。\n矢印キーで曲線上の\n点をトレース可能。"
    },
    {
        "id": "Tab_Table",
        "en_keys": ["Table", "Display values"],
        "en_desc": "Switch to the Table tab to inspect numerical values of functions or sequences.",
        "ja_title": "数値表(テーブル)",
        "ja_desc": "xやnを変化させた\nときの関数値f(x)や\n数列値u(n)の一覧表\nを表示します。"
    },
    {
        "id": "Tab_Data",
        "en_keys": ["Data"],
        "en_desc": "Data table tab for entering values V1/X1 and frequencies N1 or Y1.",
        "ja_title": "データ入力タブ",
        "ja_desc": "統計データ(値Vと\n度数N)や回帰データ\n(X,Y)を表形式で\n入力・編集します。"
    },
    {
        "id": "Tab_Stats",
        "en_keys": ["Stats"],
        "en_desc": "Summary statistics tab showing mean, standard deviation, quartiles, etc.",
        "ja_title": "統計量一覧タブ",
        "ja_desc": "入力データから算出\nされた平均値、分散\n標準偏差、四分位数\n等の一覧を表示。"
    },
    {
        "id": "Tab_Histogram",
        "en_keys": ["Histogram", "Histogram settings", "Bin width"],
        "en_desc": "Histogram plot tab and bin width / start settings.",
        "ja_title": "ヒストグラム(柱状図)",
        "ja_desc": "データの度数分布を\nヒストグラムで表示\nします。階級の幅\n(Bin width)や開始値\nも変更可能です。"
    },
    {
        "id": "Tab_Box",
        "en_keys": ["Box"],
        "en_desc": "Box-and-whisker plot showing minimum, Q1, median, Q3, and maximum.",
        "ja_title": "箱ひげ図タブ",
        "ja_desc": "データの最小値、\n第1四分位数(Q1)、\n中央値、第3四分位数\n(Q3)、最大値を箱ひ\nげ図で比較します。"
    },
    {
        "id": "Graph_NavigateZoom",
        "en_keys": ["Navigate", "Pan:", "Zoom:", "Axes", "Equal axes"],
        "en_desc": "Pan or zoom the graph viewport, adjust X/Y axis ranges, or normalize axes to 1:1 ratio.",
        "ja_title": "グラフの表示調整",
        "ja_desc": "・Navigate: 画面移動\n　とズーム操作\n・Axes: X軸/Y軸の\n　表示範囲を指定\n・Equal axes: 縦横の\n　目盛り比を1:1に"
    },
    {
        "id": "Graph_Calculate",
        "en_keys": ["Calculate", "Goto", "Go to", "Plot options", "Function options", "Sequence options"],
        "en_desc": "Open calculation/analysis tools on the graph (roots, extrema, tangent, integral, intersection) or jump to X.",
        "ja_title": "グラフ解析・設定",
        "ja_desc": "曲線上の特定xへの\n移動(Go to)や、零点\n極値、交点、接線、\n定積分の解析メニュ\nーを開きます。"
    },
    {
        "id": "Graph_Zeros",
        "en_keys": ["Zeros"],
        "en_desc": "Find x-intercepts (roots where f(x) = 0) on the graph.",
        "ja_title": "零点 (f(x)=0の解)",
        "ja_desc": "グラフがx軸と交わ\nる点(f(x)=0となる\n実数解x)を探索しま\nす。"
    },
    {
        "id": "Graph_Tangent",
        "en_keys": ["Tangent"],
        "en_desc": "Draw the tangent line to the curve at a point and display its equation y = ax + b and derivative f'(x).",
        "ja_title": "接線と微分係数",
        "ja_desc": "曲線上の点における\n接線を描画し、微分\n係数(傾き)f'(x)と\n接線の方程式を表示\nします。"
    },
    {
        "id": "Graph_Intersection",
        "en_keys": ["Intersection"],
        "en_desc": "Find intersection points between two plotted curves f(x) = g(x).",
        "ja_title": "2曲線の交点探索",
        "ja_desc": "2つの関数のグラフ\nが交わる交点座標\n(x, y) を探索しま\nす。"
    },
    {
        "id": "Graph_Preimage",
        "en_keys": ["Inverse image"],
        "en_desc": "Find x such that f(x) equals a given target value y.",
        "ja_title": "逆像 (yからxを探索)",
        "ja_desc": "指定したyの値に対\nして f(x)=y となる\nxの値をグラフ上で\n逆算探索します。"
    },
    {
        "id": "Func_Manage",
        "en_keys": [
            "Add function", "Delete function", "Curve type", "Plot range",
            "Function color", "Derivative function column", "Hide the derivative function",
            "Turn on/off", "Rename",
            "Red", "Blue", "Green", "Yellow", "Purple", "Cyan", "Pink", "Orange"
        ],
        "en_desc": "Add or delete functions, choose curve type (Cartesian, Polar, Parametric), set domain, or toggle f'(x) column.",
        "ja_title": "関数の追加と設定",
        "ja_desc": "関数の追加・削除、\n曲線種別(直交/極座標\n/媒介変数)、描画範囲\n色、導関数f'(x)列の\n表示を設定します。"
    },
    {
        "id": "Seq_Manage",
        "en_keys": [
            "Add sequence", "Choose sequence type", "Sequence type",
            "Explicit expression", "Recursive first order", "Recursive second order",
            "Sequence color", "Delete sequence", "Sum of terms", "First term index", "n column"
        ],
        "en_desc": "Define explicit u(n), 1st-order recursive u(n+1), or 2nd-order recursive u(n+2) sequences, and compute sum of terms.",
        "ja_title": "数列の定義と設定",
        "ja_desc": "・Explicit: 一般項\n・Recursive 1st:\n　2項間漸化式u(n+1)\n・Recursive 2nd:\n　3項間漸化式u(n+2)\n・Sum: 項の和を計算"
    },
    {
        "id": "Table_Manage",
        "en_keys": [
            "Set the interval", "X start", "X end", "N start", "N end",
            "T start", "T end", "θ start", "θ end", "Step",
            "Column options", "Clear column", "Fill with a formula",
            "Sort by increasing values", "Sort by increasing frequencies", "Export the column to a list"
        ],
        "en_desc": "Configure table start/end/step intervals, fill columns with formulas, sort data, or clear columns.",
        "ja_title": "表の範囲と列操作",
        "ja_desc": "表の開始値・終了値\n・刻み幅(Step)の\n設定や、列のクリア\n数式での一括入力、\n並べ替えを行います"
    },

    # =========================================================================
    # 14. 統計・回帰・確率・方程式ソルバー・Pythonの各項目
    # =========================================================================
    {
        "id": "Stats_Mean",
        "en_keys": ["Mean", "μ: Mean"],
        "en_desc": "Arithmetic mean (average) of the dataset or distribution mean mu.",
        "ja_title": "平均値 (Mean / μ)",
        "ja_desc": "データの算術平均値\nまたは確率分布の\n母平均 μ です。"
    },
    {
        "id": "Stats_StdDev",
        "en_keys": ["Standard deviation", "Standard deviation σ", "Sample std deviation s", "σ: Standard deviation"],
        "en_desc": "Population standard deviation sigma or sample unbiased standard deviation s.",
        "ja_title": "標準偏差 (σ / s)",
        "ja_desc": "データのばらつきを\n表す標準偏差です。\n・σ: 母標準偏差\n・s: 標本標準偏差"
    },
    {
        "id": "Stats_Variance",
        "en_keys": ["Variance", "Covariance"],
        "en_desc": "Variance sigma^2 (average of squared deviations from the mean) or covariance Cov(X,Y).",
        "ja_title": "分散 / 共分散",
        "ja_desc": "・Variance: 分散σ^2\n　(標準偏差の2乗)\n・Covariance:\n　2変数の共分散"
    },
    {
        "id": "Stats_Quartiles",
        "en_keys": ["First quartile", "Median", "Third quartile", "Interquartile range", "Range"],
        "en_desc": "Order statistics: Q1 (25%), Median (50%), Q3 (75%), IQR = Q3 - Q1, and Range = Max - Min.",
        "ja_title": "中央値・四分位数",
        "ja_desc": "・First quartile: Q1\n・Median: 中央値(Me)\n・Third quartile: Q3\n・Interquartile:\n　四分位範囲(Q3-Q1)\n・Range: 範囲(最大-最小)"
    },
    {
        "id": "Stats_SummaryOther",
        "en_keys": [
            "Number of data points", "Number of points",
            "Sum of values", "Sum of squared values", "Sum of squares",
            "Geometric mean", "Harmonic Mean", "Mode"
        ],
        "en_desc": "Total count N, sum of values, sum of squares, geometric mean, harmonic mean, and mode.",
        "ja_title": "データ数・各種平均",
        "ja_desc": "データの総数N、総和\n二乗和、幾何平均、\n調和平均、最頻値\n(Mode)を表示します"
    },
    {
        "id": "Reg_Models",
        "en_keys": [
            "Regression curve", "Prediction given X", "Prediction given Y",
            "Proportional", "Quadratic", "Cubic", "Quartic",
            "Logarithmic", "Exponential", "Trigonometric", "Logistic"
        ],
        "en_desc": "Regression models (Linear, Proportional, Quadratic, Cubic, Quartic, Logarithmic, Exponential, Power, Trigonometric, Logistic) and X/Y predictions.",
        "ja_title": "回帰モデルと予測",
        "ja_desc": "散布図に当てはめる\n回帰曲線(比例/2次/\n3次/4次/対数/指数/\n三角/ロジスティック)\nの選択と、X・Yの\n予測値計算です。"
    },
    {
        "id": "Proba_Distributions",
        "en_keys": [
            "Choose the distribution", "Choose parameters", "Calculate probabilities",
            "Uniform", "Uniform distribution",
            "Exponential distribution",
            "Geometric", "Geometric distribution",
            "Poisson", "Poisson distribution",
            "Chi-squared", "Chi-squared distribution",
            "Student's distribution", "F distribution",
            "n: Number of trials", "p: Success probability", "[a,b]: Interval",
            "λ: Rate parameter", "λ: Parameter", "k: Degrees of freedom",
            "d1: Degrees of freedom of the numerator", "d2: Degrees of freedom of the denominator"
        ],
        "en_desc": "Probability app distributions (Binomial, Uniform, Exponential, Normal, Chi-squared, Geometric, Poisson, Student's t, Fisher F) and parameters.",
        "ja_title": "確率分布と母数設定",
        "ja_desc": "確率分布の種類と\nパラメータ(試行回数\nn、確率p、平均μ、\n標準偏差σ、自由度k\nなど)を設定して確率\nを計算します。"
    },
    {
        "id": "Solver_Items",
        "en_keys": [
            "Add equation", "Solve the equation", "Solve the system",
            "Use an equation template", "Solution", "Approximate solution", "Search interval",
            "Enter an equation"
        ],
        "en_desc": "Equations Solver: add equations (using '=' via G key or Shift+pi), solve algebraically or numerically on an interval.",
        "ja_title": "方程式の入力と求解",
        "ja_desc": "方程式(例: x^2-2=0)\nを追加し、下部ボタン\n【Solve】で解きます\n複数式を入力すると\n連立方程式として\n求解します。"
    },
    {
        "id": "Atomic_Types",
        "en_keys": [
            "Symbol", "Protons", "Neutrons", "Type", "Molar mass", "Electronegativity", "EC",
            "Alkali metal", "Alkaline earth metal", "Lanthanide", "Actinide",
            "Transition metal", "Post-transition metal", "Metalloid", "Halogen",
            "Reactive non metal", "Noble gas"
        ],
        "en_desc": "Element properties and chemical families in the Periodic Table.",
        "ja_title": "元素の分類と特性",
        "ja_desc": "・Protons: 陽子数(Z)\n・Neutrons: 中性子数\n・Molar mass: 原子量\n・Electronegativity:\n　電気陰性度\n・EC: 電子配置"
    },
    {
        "id": "Python_Items",
        "en_keys": [
            "Add a script", "Python shell", "Execute script", "Script options",
            "Auto import in shell", "Autocomplete", "Duplicate script", "Delete script",
            "Functions and variables", "Catalog", "Modules", "Loops and tests",
            "Files", "Exceptions", "Builtins and keywords", "Imported modules and scripts"
        ],
        "en_desc": "Python script management, interactive shell, and code toolbox catalog.",
        "ja_title": "Pythonスクリプト",
        "ja_desc": "・Execute: 実行\n・Python shell:\n　対話型シェル起動\n・Script options:\n　複製・削除・設定\n・F1: 構文カタログ"
    },
    {
        "id": "Common_Buttons",
        "en_keys": ["Confirm", "Cancel", "Next"],
        "en_desc": "Confirm (OK) or Cancel dialog action.",
        "ja_title": "決定・キャンセル",
        "ja_desc": "【Enter】で決定・\n実行します。"
    }
]

# Add all 118 chemical elements automatically so Periodic Table & Toolbox Chemistry Molar Masses show Japanese names!
ELEMENTS_JA = [
    (1, "H", "Hydrogen", "水素", "非金属 / 1族\n原子量: 1.008"),
    (2, "He", "Helium", "ヘリウム", "貴ガス / 18族\n原子量: 4.0026"),
    (3, "Li", "Lithium", "リチウム", "アルカリ金属\n原子量: 6.94"),
    (4, "Be", "Beryllium", "ベリリウム", "アルカリ土類金属\n原子量: 9.0122"),
    (5, "B", "Boron", "ホウ素", "半金属 / 13族\n原子量: 10.81"),
    (6, "C", "Carbon", "炭素", "非金属 / 14族\n原子量: 12.011"),
    (7, "N", "Nitrogen", "窒素", "非金属 / 15族\n原子量: 14.007"),
    (8, "O", "Oxygen", "酸素", "非金属 / 16族\n原子量: 15.999"),
    (9, "F", "Fluorine", "フッ素", "ハロゲン / 17族\n原子量: 18.998"),
    (10, "Ne", "Neon", "ネオン", "貴ガス / 18族\n原子量: 20.180"),
    (11, "Na", "Sodium", "ナトリウム", "アルカリ金属\n原子量: 22.990"),
    (12, "Mg", "Magnesium", "マグネシウム", "アルカリ土類金属\n原子量: 24.305"),
    (13, "Al", "Aluminium", "アルミニウム", "典型金属 / 13族\n原子量: 26.982"),
    (14, "Si", "Silicon", "ケイ素", "半金属 / 14族\n原子量: 28.085"),
    (15, "P", "Phosphorus", "リン", "非金属 / 15族\n原子量: 30.974"),
    (16, "S", "Sulfur", "硫黄", "非金属 / 16族\n原子量: 32.06"),
    (17, "Cl", "Chlorine", "塩素", "ハロゲン / 17族\n原子量: 35.45"),
    (18, "Ar", "Argon", "アルゴン", "貴ガス / 18族\n原子量: 39.948"),
    (19, "K", "Potassium", "カリウム", "アルカリ金属\n原子量: 39.098"),
    (20, "Ca", "Calcium", "カルシウム", "アルカリ土類金属\n原子量: 40.078"),
    (21, "Sc", "Scandium", "スカンジウム", "遷移金属 / 3族\n原子量: 44.956"),
    (22, "Ti", "Titanium", "チタン", "遷移金属 / 4族\n原子量: 47.867"),
    (23, "V", "Vanadium", "バナジウム", "遷移金属 / 5族\n原子量: 50.942"),
    (24, "Cr", "Chromium", "クロム", "遷移金属 / 6族\n原子量: 51.996"),
    (25, "Mn", "Manganese", "マンガン", "遷移金属 / 7族\n原子量: 54.938"),
    (26, "Fe", "Iron", "鉄 (Fe)", "遷移金属 / 8族\n原子量: 55.845"),
    (27, "Co", "Cobalt", "コバルト", "遷移金属 / 9族\n原子量: 58.933"),
    (28, "Ni", "Nickel", "ニッケル", "遷移金属 / 10族\n原子量: 58.693"),
    (29, "Cu", "Copper", "銅 (Cu)", "遷移金属 / 11族\n原子量: 63.546"),
    (30, "Zn", "Zinc", "亜鉛 (Zn)", "典型金属 / 12族\n原子量: 65.38"),
    (31, "Ga", "Gallium", "ガリウム", "典型金属 / 13族\n原子量: 69.723"),
    (32, "Ge", "Germanium", "ゲルマニウム", "半金属 / 14族\n原子量: 72.63"),
    (33, "As", "Arsenic", "ヒ素 (As)", "半金属 / 15族\n原子量: 74.922"),
    (34, "Se", "Selenium", "セレン", "非金属 / 16族\n原子量: 78.971"),
    (35, "Br", "Bromine", "臭素 (Br)", "ハロゲン / 17族\n原子量: 79.904"),
    (36, "Kr", "Krypton", "クリプトン", "貴ガス / 18族\n原子量: 83.798"),
    (37, "Rb", "Rubidium", "ルビジウム", "アルカリ金属\n原子量: 85.468"),
    (38, "Sr", "Strontium", "ストロンチウム", "アルカリ土類金属\n原子量: 87.62"),
    (39, "Y", "Yttrium", "イットリウム", "遷移金属 / 3族\n原子量: 88.906"),
    (40, "Zr", "Zirconium", "ジルコニウム", "遷移金属 / 4族\n原子量: 91.224"),
    (41, "Nb", "Niobium", "ニオブ", "遷移金属 / 5族\n原子量: 92.906"),
    (42, "Mo", "Molybdenum", "モリブデン", "遷移金属 / 6族\n原子量: 95.95"),
    (43, "Tc", "Technetium", "テクネチウム", "遷移金属 / 7族\n原子量: 98"),
    (44, "Ru", "Ruthenium", "ルテニウム", "遷移金属 / 8族\n原子量: 101.07"),
    (45, "Rh", "Rhodium", "ロジウム", "遷移金属 / 9族\n原子量: 102.91"),
    (46, "Pd", "Palladium", "パラジウム", "遷移金属 / 10族\n原子量: 106.42"),
    (47, "Ag", "Silver", "銀 (Ag)", "遷移金属 / 11族\n原子量: 107.87"),
    (48, "Cd", "Cadmium", "カドミウム", "典型金属 / 12族\n原子量: 112.41"),
    (49, "In", "Indium", "インジウム", "典型金属 / 13族\n原子量: 114.82"),
    (50, "Sn", "Tin", "スズ (Sn)", "典型金属 / 14族\n原子量: 118.71"),
    (51, "Sb", "Antimony", "アンチモン", "半金属 / 15族\n原子量: 121.76"),
    (52, "Te", "Tellurium", "テルル", "半金属 / 16族\n原子量: 127.60"),
    (53, "I", "Iodine", "ヨウ素 (I)", "ハロゲン / 17族\n原子量: 126.90"),
    (54, "Xe", "Xenon", "キセノン", "貴ガス / 18族\n原子量: 131.29"),
    (55, "Cs", "Caesium", "セシウム", "アルカリ金属\n原子量: 132.91"),
    (56, "Ba", "Barium", "バリウム", "アルカリ土類金属\n原子量: 137.33"),
    (57, "La", "Lanthanum", "ランタン", "ランタノイド\n原子量: 138.91"),
    (58, "Ce", "Cerium", "セリウム", "ランタノイド\n原子量: 140.12"),
    (59, "Pr", "Praseodymium", "プラセオジム", "ランタノイド\n原子量: 140.91"),
    (60, "Nd", "Neodymium", "ネオジム", "ランタノイド\n原子量: 144.24"),
    (61, "Pm", "Promethium", "プロメチウム", "ランタノイド\n原子量: 145"),
    (62, "Sm", "Samarium", "サマリウム", "ランタノイド\n原子量: 150.36"),
    (63, "Eu", "Europium", "ユウロピウム", "ランタノイド\n原子量: 151.96"),
    (64, "Gd", "Gadolinium", "ガドリニウム", "ランタノイド\n原子量: 157.25"),
    (65, "Tb", "Terbium", "テルビウム", "ランタノイド\n原子量: 158.93"),
    (66, "Dy", "Dysprosium", "ジスプロシウム", "ランタノイド\n原子量: 162.50"),
    (67, "Ho", "Holmium", "ホルミウム", "ランタノイド\n原子量: 164.93"),
    (68, "Er", "Erbium", "エルビウム", "ランタノイド\n原子量: 167.26"),
    (69, "Tm", "Thulium", "ツリウム", "ランタノイド\n原子量: 168.93"),
    (70, "Yb", "Ytterbium", "イッテルビウム", "ランタノイド\n原子量: 173.05"),
    (71, "Lu", "Lutetium", "ルテチウム", "ランタノイド\n原子量: 174.97"),
    (72, "Hf", "Hafnium", "ハフニウム", "遷移金属 / 4族\n原子量: 178.49"),
    (73, "Ta", "Tantalum", "タンタル", "遷移金属 / 5族\n原子量: 180.95"),
    (74, "W", "Tungsten", "タングステン", "遷移金属 / 6族\n原子量: 183.84"),
    (75, "Re", "Rhenium", "レニウム", "遷移金属 / 7族\n原子量: 186.21"),
    (76, "Os", "Osmium", "オスミウム", "遷移金属 / 8族\n原子量: 190.23"),
    (77, "Ir", "Iridium", "イリジウム", "遷移金属 / 9族\n原子量: 192.22"),
    (78, "Pt", "Platinum", "白金 (Pt)", "遷移金属 / 10族\n原子量: 195.08"),
    (79, "Au", "Gold", "金 (Au)", "遷移金属 / 11族\n原子量: 196.97"),
    (80, "Hg", "Mercury", "水銀 (Hg)", "典型金属 / 12族\n原子量: 200.59"),
    (81, "Tl", "Thallium", "タリウム", "典型金属 / 13族\n原子量: 204.38"),
    (82, "Pb", "Lead", "鉛 (Pb)", "典型金属 / 14族\n原子量: 207.2"),
    (83, "Bi", "Bismuth", "ビスマス", "典型金属 / 15族\n原子量: 208.98"),
    (84, "Po", "Polonium", "ポロニウム", "典型金属 / 16族\n原子量: 209"),
    (85, "At", "Astatine", "アスタチン", "ハロゲン / 17族\n原子量: 210"),
    (86, "Rn", "Radon", "ラドン", "貴ガス / 18族\n原子量: 222"),
    (87, "Fr", "Francium", "フランシウム", "アルカリ金属\n原子量: 223"),
    (88, "Ra", "Radium", "ラジウム", "アルカリ土類金属\n原子量: 226"),
    (89, "Ac", "Actinium", "アクチニウム", "アクチノイド\n原子量: 227"),
    (90, "Th", "Thorium", "トリウム", "アクチノイド\n原子量: 232.04"),
    (91, "Pa", "Protactinium", "プロトアクチニウム", "アクチノイド\n原子量: 231.04"),
    (92, "U", "Uranium", "ウラン (U)", "アクチノイド\n原子量: 238.03"),
    (93, "Np", "Neptunium", "ネプツニウム", "アクチノイド\n原子量: 237"),
    (94, "Pu", "Plutonium", "プルトニウム", "アクチノイド\n原子量: 244"),
    (95, "Am", "Americium", "アメリシウム", "アクチノイド\n原子量: 243"),
    (96, "Cm", "Curium", "キュリウム", "アクチノイド\n原子量: 247"),
    (97, "Bk", "Berkelium", "バークリウム", "アクチノイド\n原子量: 247"),
    (98, "Cf", "Californium", "カリホルニウム", "アクチノイド\n原子量: 251"),
    (99, "Es", "Einsteinium", "アインスタイニウム", "アクチノイド\n原子量: 252"),
    (100, "Fm", "Fermium", "フェルミウム", "アクチノイド\n原子量: 257"),
    (101, "Md", "Mendelevium", "メンデレビウム", "アクチノイド\n原子量: 258"),
    (102, "No", "Nobelium", "ノーベリウム", "アクチノイド\n原子量: 259"),
    (103, "Lr", "Lawrencium", "ローレンシウム", "アクチノイド\n原子量: 266"),
    (104, "Rf", "Rutherfordium", "ラザホージウム", "遷移金属 / 4族\n原子量: 267"),
    (105, "Db", "Dubnium", "ドブニウム", "遷移金属 / 5族\n原子量: 268"),
    (106, "Sg", "Seaborgium", "シーボーギウム", "遷移金属 / 6族\n原子量: 269"),
    (107, "Bh", "Bohrium", "ボーリウム", "遷移金属 / 7族\n原子量: 270"),
    (108, "Hs", "Hassium", "ハッシウム", "遷移金属 / 8族\n原子量: 277"),
    (109, "Mt", "Meitnerium", "マイトネリウム", "9族元素\n原子量: 278"),
    (110, "Ds", "Darmstadtium", "ダルムスタチウム", "10族元素\n原子量: 281"),
    (111, "Rg", "Roentgenium", "レントゲニウム", "11族元素\n原子量: 282"),
    (112, "Cn", "Copernicium", "コペルニシウム", "12族元素\n原子量: 285"),
    (113, "Nh", "Nihonium", "ニホニウム", "13族 / 日本発見\n原子量: 286"),
    (114, "Fl", "Flerovium", "フレロビウム", "14族元素\n原子量: 289"),
    (115, "Mc", "Moscovium", "モスコビウム", "15族元素\n原子量: 289"),
    (116, "Lv", "Livermorium", "リバモリウム", "16族元素\n原子量: 293"),
    (117, "Ts", "Tennessine", "テネシン", "17族元素\n原子量: 294"),
    (118, "Og", "Oganesson", "オガネソン", "18族元素\n原子量: 294"),
]

for z, sym, en_name, ja_name, ja_info in ELEMENTS_JA:
    keys = [
        en_name,
        "{} - {} ({})".format(z, en_name, sym),
        "{} - {} ({})".format(sym, en_name, z),
    ]
    if en_name == "Ruthenium":
        keys.append("Ruthemium")
    elif en_name == "Iodine":
        keys.append("Iodure")
    elif en_name == "Caesium":
        keys.extend(["Cesium", "55 - Cesium (Cs)", "Cs - Cesium (55)"])
    elif en_name == "Dysprosium":
        keys.append("Dyxprosium")
    elif en_name == "Lutetium":
        keys.extend(["Lutecium", "71 - Lutecium (Lu)", "Lu - Lutecium (71)"])
    elif en_name == "Thallium":
        keys.append("Thalium")
    elif en_name == "Gallium":
        keys.append("Ga - Gallium (32)")
    elif en_name == "Francium":
        keys.append("Fr - Francium (223)")

    RAW_HELP_ENTRIES.append({
        "id": "Elem_{:03d}_{}".format(z, sym),
        "en_keys": keys,
        "en_desc": "Element {} ({}) - Atomic number {}.".format(en_name, sym, z),
        "ja_title": ja_name,
        "ja_desc": "原子番号: {} ({})\n{}".format(z, sym, ja_info)
    })


def main():
    import sys
    json_path = os.path.join(SCRIPT_DIR, "help_dict.json")
    regenerate = ("--regenerate-default" in sys.argv) or (not os.path.exists(json_path))

    if regenerate:
        json_dict = {}
        for entry in RAW_HELP_ENTRIES:
            entry_id = entry["id"]
            en_keys = [k.strip() for k in entry["en_keys"] if k.strip()]
            ja_title = entry["ja_title"].strip()
            ja_desc = format_ja_desc(entry["ja_desc"], max_units=16)

            json_dict[entry_id] = {
                "en_keys": en_keys,
                "en_desc": entry["en_desc"],
                "ja_title": ja_title,
                "ja_desc": ja_desc
            }
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(json_dict, f, ensure_ascii=False, indent=2)
        print("Saved default JSON dictionary ({} items) to {}".format(len(json_dict), json_path))
    else:
        with open(json_path, "r", encoding="utf-8") as f:
            json_dict = json.load(f)
        print("Loaded JSON dictionary ({} items) from {}".format(len(json_dict), json_path))

    cpp_entries = []
    seen_keys = set()
    for entry_id, entry in json_dict.items():
        en_keys = [k.strip() for k in entry.get("en_keys", []) if k.strip()]
        ja_title = entry.get("ja_title", "").strip()
        ja_desc = format_ja_desc(entry.get("ja_desc", ""), max_units=16)
        for k in en_keys:
            if k not in seen_keys:
                seen_keys.add(k)
                cpp_entries.append((k, ja_title, ja_desc))

    # Generate help_data.h
    header_path = os.path.join(SCRIPT_DIR, "help_data.h")
    with open(header_path, "w", encoding="utf-8") as f:
        f.write("""#ifndef ION_SIMULATOR_BRAIN_HELP_DATA_H
#define ION_SIMULATOR_BRAIN_HELP_DATA_H

#include <stddef.h>
#include <string.h>
#include <wchar.h>

namespace Ion {
namespace Simulator {
namespace Display {

struct HelpEntry {
  const char * enKey;
  const wchar_t * jaTitle;
  const wchar_t * jaDesc;
};

static constexpr HelpEntry kHelpEntries[] = {
""")
        for en_key, ja_title, ja_desc in cpp_entries:
            en_esc = en_key.replace("\\", "\\\\").replace('"', '\\"')
            title_esc = ja_title.replace("\\", "\\\\").replace('"', '\\"')
            desc_esc = ja_desc.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
            f.write('  {{ "{en}", L"{title}", L"{desc}" }},\n'.format(
                en=en_esc, title=title_esc, desc=desc_esc
            ))

        f.write("""};

static constexpr int kHelpEntriesCount = static_cast<int>(sizeof(kHelpEntries) / sizeof(kHelpEntries[0]));

static inline bool equalsTrimmed(const char * input, const char * key) {
  if (!input || !key) {
    return false;
  }
  while (*input == ' ') {
    input++;
  }
  size_t inputLen = strlen(input);
  while (inputLen > 0 && input[inputLen - 1] == ' ') {
    inputLen--;
  }
  size_t keyLen = strlen(key);
  if (inputLen != keyLen) {
    return false;
  }
  return strncmp(input, key, keyLen) == 0;
}

static inline int findHelpEntryIndex(const char * text) {
  if (!text || text[0] == '\\0') {
    return -1;
  }
  for (int i = 0; i < kHelpEntriesCount; i++) {
    if (equalsTrimmed(text, kHelpEntries[i].enKey)) {
      return i;
    }
  }
  return -1;
}

}
}
}

#endif
""")
    print("Generated C++ header ({} lookup keys) at {}".format(len(cpp_entries), header_path))


if __name__ == "__main__":
    main()
