# -*- coding: utf-8 -*-
"""
Generates help_dict.json (with English & Japanese side-by-side)
and compiles it into help_data.h for BrainCalc right sidebar display.
"""

import os
import re
import json

def load_i18n(path):
    d = {}
    if not os.path.exists(path):
        return d
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = re.match(r'^(\w+)\s*=\s*"(.*)"', line.strip())
            if m:
                d[m.group(1)] = m.group(2)
    return d

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

toolbox_en = load_i18n(os.path.join(ROOT_DIR, "apps", "toolbox.en.i18n"))
shared_en = load_i18n(os.path.join(ROOT_DIR, "apps", "shared.en.i18n"))

# Comprehensive dictionary of core NumWorks functions
# Each entry contains en_label, en_desc, ja_title, and ja_desc.
HELP_DATABASE = {
    # 微積分・解析 (Calculus)
    "DiffCommandWithArg": {
        "en_label": "diff(f(x),x,a)",
        "en_desc": "Derivative value",
        "ja_title": "微分係数",
        "ja_desc": "f(x)の x=a における\n微分係数を計算します。\n\n【書式】\ndiff(式, 変数, 値)\n\n【例】\ndiff(x^2, x, 3) → 6"
    },
    "IntCommandWithArg": {
        "en_label": "int(f(x),x,a,b)",
        "en_desc": "Integral",
        "ja_title": "定積分",
        "ja_desc": "区間[a, b]における\n関数の定積分を\n数値計算します。\n\n【書式】\nint(式, 変数, a, b)\n\n【例】\nint(x^2, x, 0, 1)\n→ 1/3"
    },
    "SumCommandWithArg": {
        "en_label": "sum(f(n),n,a,b)",
        "en_desc": "Sum",
        "ja_title": "総和 (シグマ)",
        "ja_desc": "n=a から b までの\nf(n) の総和を\n計算します。\n\n【書式】\nsum(式, 変数, a, b)\n\n【例】\nsum(n, n, 1, 10)\n→ 55"
    },
    "ProductCommandWithArg": {
        "en_label": "prod(f(n),n,a,b)",
        "en_desc": "Product",
        "ja_title": "総乗 (プロダクト)",
        "ja_desc": "n=a から b までの\nf(n) の掛け算の総積\nを計算します。\n\n【書式】\nprod(式, 変数, a, b)\n\n【例】\nprod(n, n, 1, 5)\n→ 120"
    },
    "Infinity": {
        "en_label": "inf",
        "en_desc": "Infinity",
        "ja_title": "無限大",
        "ja_desc": "極限や定積分の区間\nなどに使用する\n正の無限大記号です。\n\n【例】\nint(1/x^2, x, 1, inf)\n→ 1"
    },

    # 複素数 (Complex numbers)
    "AbsCommandWithArg": {
        "en_label": "abs(z)",
        "en_desc": "Absolute value",
        "ja_title": "絶対値 / 大きさ",
        "ja_desc": "実数の絶対値、または\n複素数 z の大きさ\n|z| を計算します。\n\n【例】\nabs(3+4i) → 5"
    },
    "ArgCommandWithArg": {
        "en_label": "arg(z)",
        "en_desc": "Argument",
        "ja_title": "偏角 (ラジアン)",
        "ja_desc": "複素数 z の偏角\nθ = arg(z) を\n計算します。\n\n【例】\narg(1+i) → π/4"
    },
    "ReCommandWithArg": {
        "en_label": "re(z)",
        "en_desc": "Real part",
        "ja_title": "実部",
        "ja_desc": "複素数 z = a + bi の\n実部 a を取り出します。\n\n【例】\nre(3+4i) → 3"
    },
    "ImCommandWithArg": {
        "en_label": "im(z)",
        "en_desc": "Imaginary part",
        "ja_title": "虚部",
        "ja_desc": "複素数 z = a + bi の\n虚部 b を取り出します。\n\n【例】\nim(3+4i) → 4"
    },
    "ConjCommandWithArg": {
        "en_label": "conj(z)",
        "en_desc": "Conjugate",
        "ja_title": "共役複素数",
        "ja_desc": "複素数 z = a + bi の\n共役複素数 a - bi を\n計算します。\n\n【例】\nconj(3+4i) → 3-4i"
    },

    # 順列・組合せ (Combinatorics)
    "BinomialCommandWithArg": {
        "en_label": "binomial(n,k)",
        "en_desc": "Combination (nCr)",
        "ja_title": "組合せ (nCr)",
        "ja_desc": "異なる n 個から k 個を\n選ぶ組合せの数\nnCk を計算します。\n\n【書式】\nbinomial(n, k)\n\n【例】\nbinomial(5, 2) → 10"
    },
    "PermuteCommandWithArg": {
        "en_label": "permute(n,k)",
        "en_desc": "Permutation (nPr)",
        "ja_title": "順列 (nPr)",
        "ja_desc": "異なる n 個から k 個を\n並べる順列の数\nnPr を計算します。\n\n【書式】\npermute(n, k)\n\n【例】\npermute(5, 2) → 20"
    },
    "FactorialCommandWithArg": {
        "en_label": "n!",
        "en_desc": "Factorial",
        "ja_title": "階乗",
        "ja_desc": "正の整数 n の階乗\nn! = n*(n-1)*...*1\nを計算します。\n\n【例】\n5! → 120"
    },

    # 確率・統計分布 (Probability Distributions)
    "NormCDFCommandWithArg": {
        "en_label": "normcdf(a,μ,σ)",
        "en_desc": "Cumulative normal",
        "ja_title": "累積正規分布",
        "ja_desc": "平均 μ、標準偏差 σ の\n正規分布において、\n負の無限大から a までの\n累積確率を計算します。"
    },
    "NormCDF2CommandWithArg": {
        "en_label": "normcdf(a,b,μ,σ)",
        "en_desc": "Cumulative normal range",
        "ja_title": "区間累積正規分布",
        "ja_desc": "平均 μ、標準偏差 σ の\n正規分布において、\n区間 [a, b] の\n確率 P(a≦X≦b) を\n計算します。"
    },
    "InvNormCommandWithArg": {
        "en_label": "invnorm(p,μ,σ)",
        "en_desc": "Inverse normal",
        "ja_title": "逆正規分布",
        "ja_desc": "累積確率が p となる\n確率変数 x の境界値\nを計算します。"
    },
    "NormPDFCommandWithArg": {
        "en_label": "normpdf(x,μ,σ)",
        "en_desc": "Normal PDF",
        "ja_title": "正規分布の確率密度",
        "ja_desc": "平均 μ、標準偏差 σ の\n正規分布における\nx の確率密度関数の値\nを計算します。"
    },
    "BinomialPDFCommandWithArg": {
        "en_label": "binompdf(m,n,p)",
        "en_desc": "Binomial PDF",
        "ja_title": "二項分布の確率",
        "ja_desc": "試行回数 n、確率 p の\n二項分布において、\n成功回数が丁度 m 回と\nなる確率を計算します。"
    },
    "BinomialCDFCommandWithArg": {
        "en_label": "binomcdf(m,n,p)",
        "en_desc": "Cumulative binomial",
        "ja_title": "累積二項分布",
        "ja_desc": "試行回数 n、確率 p の\n二項分布において、\n成功回数が 0 から m 回\n以下となる確率を計算します。"
    },
    "InvBinomialCommandWithArg": {
        "en_label": "invbinom(α,n,p)",
        "en_desc": "Inverse binomial",
        "ja_title": "逆二項分布",
        "ja_desc": "累積確率が α 以上と\nなる最小の成功回数 m\nを計算します。"
    },

    # 整数・数論 (Arithmetic)
    "GcdCommandWithArg": {
        "en_label": "gcd(a,b)",
        "en_desc": "Greatest common divisor",
        "ja_title": "最大公約数 (GCD)",
        "ja_desc": "2つ以上の整数の\n最大公約数を\n計算します。\n\n【例】\ngcd(24, 36) → 12"
    },
    "LcmCommandWithArg": {
        "en_label": "lcm(a,b)",
        "en_desc": "Least common multiple",
        "ja_title": "最小公倍数 (LCM)",
        "ja_desc": "2つ以上の整数の\n最小公倍数を\n計算します。\n\n【例】\nlcm(12, 18) → 36"
    },
    "RemCommandWithArg": {
        "en_label": "rem(a,b)",
        "en_desc": "Remainder",
        "ja_title": "剰余 (余り)",
        "ja_desc": "整数 a を b で割った\nときの余り (a mod b)\nを計算します。\n\n【例】\nrem(17, 5) → 2"
    },
    "QuoCommandWithArg": {
        "en_label": "quo(a,b)",
        "en_desc": "Quotient",
        "ja_title": "商 (整数の商)",
        "ja_desc": "整数 a を b で割った\nときの整数の商\n(切り捨て) を計算します。\n\n【例】\nquo(17, 5) → 3"
    },
    "FracCommandWithArg": {
        "en_label": "frac(x)",
        "en_desc": "Fractional part",
        "ja_title": "小数部分",
        "ja_desc": "実数 x の小数部分\n(x - floor(x)) を\n取り出します。\n\n【例】\nfrac(3.14) → 0.14"
    },
    "FloorCommandWithArg": {
        "en_label": "floor(x)",
        "en_desc": "Floor function",
        "ja_title": "床関数 (ガウス記号)",
        "ja_desc": "実数 x 以下の最大の\n整数 [x] を計算します。\n\n【例】\nfloor(3.8) → 3\nfloor(-2.3) → -3"
    },
    "CeilCommandWithArg": {
        "en_label": "ceil(x)",
        "en_desc": "Ceiling function",
        "ja_title": "天井関数",
        "ja_desc": "実数 x 以上の最小の\n整数を計算します。\n\n【例】\nceil(3.2) → 4\nceil(-2.8) → -2"
    },
    "RoundCommandWithArg": {
        "en_label": "round(x,n)",
        "en_desc": "Round",
        "ja_title": "四捨五入",
        "ja_desc": "数 x を小数点以下\nn 桁に四捨五入します。\n\n【書式】\nround(x, 桁数)\n\n【例】\nround(3.14159, 2)\n→ 3.14"
    },

    # 行列・ベクトル (Matrices & Vectors)
    "DeterminantCommandWithArg": {
        "en_label": "det(M)",
        "en_desc": "Determinant",
        "ja_title": "行列式 (det)",
        "ja_desc": "正方行列 M の\n行列式 det(M) を\n計算します。\n\n【例】\ndet([[1,2],[3,4]])\n→ -2"
    },
    "InverseCommandWithArg": {
        "en_label": "inverse(M)",
        "en_desc": "Matrix inverse",
        "ja_title": "逆行列",
        "ja_desc": "正方行列 M の\n逆行列 M^-1 を\n計算します。\n\n(行列 M の -1 乗でも\n計算可能です)"
    },
    "TransposeCommandWithArg": {
        "en_label": "transpose(M)",
        "en_desc": "Transpose",
        "ja_title": "転置行列",
        "ja_desc": "行列 M の行と列を\n入れ替えた転置行列\n^t M を計算します。"
    },
    "TraceCommandWithArg": {
        "en_label": "trace(M)",
        "en_desc": "Trace",
        "ja_title": "トレース (対角和)",
        "ja_desc": "正方行列 M の\n主対角成分の総和\n(対角和) を計算します。"
    },
    "DimensionCommandWithArg": {
        "en_label": "dim(M)",
        "en_desc": "Dimension / size",
        "ja_title": "行列・ベクトルの次元",
        "ja_desc": "行列 M の行数・列数、\nまたはベクトルの長さを\nリスト {行, 列} で\n返します。"
    },
    "DotCommandWithArg": {
        "en_label": "dot(u,v)",
        "en_desc": "Dot product",
        "ja_title": "内積 (ドット積)",
        "ja_desc": "2つのベクトル u, v の\n内積 (スカラー積)\nu・v を計算します。"
    },
    "CrossCommandWithArg": {
        "en_label": "cross(u,v)",
        "en_desc": "Cross product",
        "ja_title": "外積 (ベクトル積)",
        "ja_desc": "3次元ベクトル u, v の\n外積 (ベクトル積)\nu×v を計算します。"
    },
    "NormVectorCommandWithArg": {
        "en_label": "norm(v)",
        "en_desc": "Euclidean norm",
        "ja_title": "ベクトルのノルム",
        "ja_desc": "ベクトル v の幾何学的\nな長さ (ユークリッドノルム\n√(|v1|^2 + |v2|^2...))\nを計算します。"
    },
    "RowEchelonFormCommandWithArg": {
        "en_label": "ref(M)",
        "en_desc": "Row echelon form",
        "ja_title": "行階段形 (REF)",
        "ja_desc": "行列 M を行基本変形\nによって行階段形に\n変換します。"
    },
    "ReducedRowEchelonFormCommandWithArg": {
        "en_label": "rref(M)",
        "en_desc": "Reduced row echelon form",
        "ja_title": "被約行階段形 (RREF)",
        "ja_desc": "連立方程式の解を求める\n掃き出し法（ガウス・ジョルダン法）\nを行い、被約行階段形に変換します。"
    },

    # 乱数 (Random)
    "RandintCommandWithArg": {
        "en_label": "randint(a,b)",
        "en_desc": "Random integer",
        "ja_title": "整数の乱数",
        "ja_desc": "a 以上 b 以下の\nランダムな整数を\n1つ生成します。\n\n【書式】\nrandint(最小, 最大)\n\n【例】\nrandint(1, 6) → 4"
    },
    "RandomCommandWithArg": {
        "en_label": "random()",
        "en_desc": "Random float",
        "ja_title": "実数の乱数 (0〜1)",
        "ja_desc": "区間 [0, 1) の一様な\n疑似乱数（実数）を\n1つ生成します。"
    }
}

# 1. Write help_dict.json
json_path = os.path.join(ROOT_DIR, "ion", "src", "simulator", "brain", "help_dict.json")
with open(json_path, "w", encoding="utf-8") as f:
    json.dump(HELP_DATABASE, f, ensure_ascii=False, indent=2)
print("Saved JSON dictionary to:", json_path)

# 2. Generate help_data.h (C++ static array)
header_path = os.path.join(ROOT_DIR, "ion", "src", "simulator", "brain", "help_data.h")
with open(header_path, "w", encoding="utf-8") as f:
    f.write(u"""#ifndef ION_SIMULATOR_BRAIN_HELP_DATA_H
#define ION_SIMULATOR_BRAIN_HELP_DATA_H

#include <apps/i18n.h>
#include <wchar.h>

namespace Ion {
namespace Simulator {
namespace Display {

struct HelpEntry {
  I18n::Message messageId;
  const wchar_t * jaTitle;
  const wchar_t * jaDesc;
};

static constexpr HelpEntry kHelpEntries[] = {
""")
    for msg_id, info in HELP_DATABASE.items():
        title_esc = info["ja_title"].replace(u'"', u'\\"')
        desc_esc = info["ja_desc"].replace(u'\\', u'\\\\').replace(u'"', u'\\"').replace(u'\n', u'\\n')
        f.write(u'  {{ I18n::Message::{msg}, L"{title}", L"{desc}" }},\n'.format(
            msg=msg_id, title=title_esc, desc=desc_esc
        ))

    f.write(u"""};

static constexpr size_t kHelpEntriesCount = sizeof(kHelpEntries) / sizeof(kHelpEntries[0]);

const HelpEntry * findHelpEntry(I18n::Message msg) {
  for (size_t i = 0; i < kHelpEntriesCount; i++) {
    if (kHelpEntries[i].messageId == msg) {
      return &kHelpEntries[i];
    }
  }
  return nullptr;
}

}
}
}

#endif
""")
print("Generated C++ header to:", header_path)

