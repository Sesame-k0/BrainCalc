#ifndef APPS_KHYCAS_KHYCAS_HELP_JA_H
#define APPS_KHYCAS_KHYCAS_HELP_JA_H

#include <stdio.h>
#include <string.h>

extern "C" {
  void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);
  void BrainClearCustomHelp();
}

namespace KhyCAS {
namespace HelpJa {

struct CategoryHelpEntry {
  int id;
  const char * title;
  const char * body;
};

// 全カテゴリ共通フォーマット:
//   【機能】 + 【代表】 + 【操作】
// ※ ヘッダは全て4文字以下、全行に日本語を含めて 144px 以内に収め小文字化を防止
static constexpr const CategoryHelpEntry kCategoryHelp[] = {
  { 0,  "すべて",
    "【機能】\nすべての組込みコマンドをABC順に一覧表示します。\n"
    "【代表】\n・英字で頭文字ジャンプ\n"
    "【操作】\n[→/Ent]開く [Esc]戻る\n[PgUp/Dn]頁移動" },
  { 1,  "オプション",
    "【機能】\nコマンドのオプション引数や定数識別子を選択します。\n"
    "【代表】\n・display / color\n・symbolic / approx\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 2,  "代数式変形",
    "【機能】\n式の展開・因数分解・通分・部分分数分解などを行います。\n"
    "【代表】\n・factor / expand\n・simplify・partfrac\n・cfactor・cpartfrac\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 3,  "微分・積分",
    "【機能】\n記号微分・不定積分・定積分・極限・級数展開・常微分方程式。\n"
    "【代表】\n・diff / integrate\n・limit / series\n・desolve / laplace\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 4,  "複素数計算",
    "【機能】\n実部・虚部・絶対値・偏角・共役や複素因数分解を行います。\n"
    "【代表】\n・abs / arg / conj\n・re・im・rect2polar\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 5,  "2Dグラフ描画",
    "【機能】\n陽関数・媒介変数・極座標・領域・陰関数グラフを描画します。\n"
    "【代表】\n・plot / plotparam\n・plotpolar・plotineq\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 6,  "実数・丸め",
    "【機能】\n浮動小数近似・分数化・床関数・天井関数・丸め処理を行います。\n"
    "【代表】\n・exact / approx\n・floor・ceil・round\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 7,  "拡張機能",
    "【機能】\n特殊な数学関数や拡張モジュール機能を参照します。\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 8,  "整数・数論",
    "【機能】\n素因数分解・GCD/LCM・合同算術・ユークリッド互除法。\n"
    "【代表】\n・ifactor / isprime\n・gcd / lcm / mod\n・iegcd / powmod\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 9,  "線形代数",
    "【機能】\n行列式・逆行列・掃き出し・固有値・固有ベクトル・階数。\n"
    "【代表】\n・det / inv / rref\n・egvl / egv / rank\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 10, "多項式操作",
    "【機能】\n係数抽出・次数・商と余り・終結式・グレブナー基底。\n"
    "【代表】\n・coeff / degree\n・quo / rem / roots\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 11, "確率・分布",
    "【機能】\n乱数・二項係数・順列・正規/二項/ポアソン分布など。\n"
    "【代表】\n・comb / perm / rand\n・normald / binomial\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 12, "プログラミング",
    "【機能】\n条件分岐・反復・関数定義・文字列・I/O 構文を扱います。\n"
    "【代表】\n・if / for / while\n・function / return\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 13, "ファイル管理",
    "【機能】\nスクリプトや計算セッションの保存・読込コマンド群です。\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 14, "式書き換え",
    "【機能】\n指数対数と三角・双曲関数を相互に変換します。\n"
    "【代表】\n・lin・tlin・texpand\n・exp2trig・trig2exp\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 15, "方程式求解",
    "【機能】\n代数方程式・連立方程式・数値解(Newton法)を求めます。\n"
    "【代表】\n・solve / csolve\n・fsolve / linsolve\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 16, "グラフと曲線",
    "【機能】\nパラメトリック・極座標・微分方程式ベクトル場を描画。\n"
    "【代表】\n・plot / plotfield\n・plotode・interactive\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 17, "行列・ベクトル",
    "【機能】\n行列生成・内積・外積・ノルム・転置などを扱います。\n"
    "【代表】\n・matrix・ranm・idn\n・dot / cross / tran\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 18, "三角関数",
    "【機能】\n三角・逆三角・双曲関数の計算および簡約を行います。\n"
    "【代表】\n・sin / cos / tan\n・asin / acos / atan\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 19, "信号・FFT",
    "【機能】\n離散フーリエ変換・畳み込み・相関などの信号処理。\n"
    "【代表】\n・fft / ifft / conv\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 20, "統計・リスト",
    "【機能】\n平均・分散・標準偏差・中央値・回帰分析・リスト操作。\n"
    "【代表】\n・mean / stddev\n・median・linear_reg\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 21, "文字列・変換",
    "【機能】\n文字列連結・長さ・数値/文字列相互変換を行います。\n"
    "【代表】\n・cat・size・string\n・expr / asc / char\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 22, "物理定数・単位",
    "【機能】\n単位付き計算と国際単位系(SI)への単位変換を行います。\n"
    "【代表】\n・mksa / usimplify\n・convert / unit\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
  { 23, "ジオメトリ",
    "【機能】\n2D/3D 点・直線・円・交点・距離の幾何計算・描画。\n"
    "【代表】\n・point・line・circle\n・inter / distance\n"
    "【操作】\n[→/Ent]開く [Esc]戻る" },
};

struct CommandHelpEntry {
  const char * name;
  const char * jaBody;
};

// 全コマンド共通フォーマット:
//   【機能】\n...\n【書式】\n構文: ...\n【例】\n例: ...\n【操作】\n...
// ※ ASCII のみの行・144px超の行を作らないことで小文字フォントを徹底回避
static constexpr const CommandHelpEntry kCommandHelp[] = {
  // === 代数式変形 ===
  { "factor",
    "【機能】\n多項式や有理式を有理数体上で因数分解します。\n"
    "【書式】\n構文: factor(式)\n構文: factor(式, 変)\n"
    "【例】\n例: factor(x^4-1)\n・factor(x^2-1,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "cfactor",
    "【機能】\n複素数範囲(iを含む)まで式を完全に因数分解します。\n"
    "【書式】\n構文: cfactor(式)\n構文: cfactor(式,変)\n"
    "【例】\n例: cfactor(x^2+1)\n例: cfactor(x^4+4)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "expand",
    "【機能】\n式の括弧を展開して単項式の和に整理します。\n"
    "【書式】\n構文: expand(式)\n"
    "【例】\n例: expand((x+y)^3)\n例: expand((x+1)^2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "simplify",
    "【機能】\n根号・分数・三角関数などを含む式を最も簡潔な形にします。\n"
    "【書式】\n構文: simplify(式)\n"
    "【例】\n例: simplify(x^2/x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "normal",
    "【機能】\n分母分子を通分展開し既約分数(標準形)にします。\n"
    "【書式】\n構文: normal(式)\n"
    "【例】\n例: normal(1/x+1/y)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "partfrac",
    "【機能】\n有理式(分数式)を実数係数の部分分数へ分解します。\n"
    "【書式】\n構文: partfrac(式)\n構文: partfrac(式,変)\n"
    "【例】\n例: 1/(x^2-1) を分解\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "cpartfrac",
    "【機能】\n有理式を複素数(1次分母)の部分分数へ分解します。\n"
    "【書式】\n構文: cpartfrac(f)\n"
    "【例】\n例: 1/(x^2+1) を分解\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "ratnormal",
    "【機能】\n有理式のみを高速に通分し既約標準形にします。\n"
    "【書式】\n構文: ratnormal(式)\n"
    "【例】\n例: (x^2-1)/x を約分\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "collect",
    "【機能】\n指定した変数のべきまたは同類項ごとにまとめます。\n"
    "【書式】\n構文: collect(式)\n構文: collect(式,変)\n"
    "【例】\n例:collect(a*x+b*x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "subst",
    "【機能】\n式の中の変数や部分式を別の値・式に置き換えます。\n"
    "【書式】\n構文: subst(式,x=値)\n"
    "【例】\n例: subst(x^2+1,x=3)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "numer",
    "【機能】\n分数・有理式の分子を取り出します。\n"
    "【書式】\n構文: numer(分数式)\n"
    "【例】\n例: numer((x+1)/x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "denom",
    "【機能】\n分数・有理式の分母を取り出します。\n"
    "【書式】\n構文: denom(分数式)\n"
    "【例】\n例: denom((x+1)/x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "canonical_form",
    "【機能】\n2次式を平方完成の形 a*(x+b)^2+c に変形します。\n"
    "【書式】\n構文:\n・canonical_form(式)\n"
    "【例】\n例:\n・canonical_form(式)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "coeff",
    "【機能】\n多項式の指定次数の係数や全係数リストを返します。\n"
    "【書式】\n構文: coeff(P, x, n)\n"
    "【例】\n例: coeff(3*x^2+x,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "degree",
    "【機能】\n多項式の最大次数を返します。\n"
    "【書式】\n構文: degree(多項式)\n構文: degree(式,変数)\n"
    "【例】\n例: degree(x^5+2*x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "quo",
    "【機能】\n多項式の除算における商を求めます。\n"
    "【書式】\n構文: quo(P, Q, 変数)\n"
    "【例】\n例: quo(x^3-1,x-1,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "rem",
    "【機能】\n多項式の除算における余りを求めます。\n"
    "【書式】\n構文: rem(P, Q, 変数)\n"
    "【例】\n例: rem(x^3+2,x-1,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "roots",
    "【機能】\n多項式の解と重複度の組を返します。\n"
    "【書式】\n構文: roots(式, 変数)\n"
    "【例】\n例: roots(x^2-1, x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "resultant",
    "【機能】\n2つの多項式の終結式(共通根の判定に利用)を求めます。\n"
    "【書式】\n構文: resultant(P,Q,変)\n"
    "【例】\n例: resultant(P,Q,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 方程式求解 ===
  { "solve",
    "【機能】\n方程式・連立方程式を記号的に解きます(文字係数可)。\n"
    "【書式】\n構文: solve(式, 変数)\n構文: solve([式],[x,y])\n"
    "【例】\n例: solve(x^2-4=0,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "csolve",
    "【機能】\n複素数範囲で方程式や連立方程式を解きます。\n"
    "【書式】\n構文: csolve(式,変数)\n"
    "【例】\n例: csolve(x^2+1=0,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "fsolve",
    "【機能】\n解析的に解けない方程式の数値近似解を求めます。\n"
    "【書式】\n構文: fsolve(式,x=値)\n"
    "【例】\n例:fsolve(cos(x)=x,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "linsolve",
    "【機能】\n連立一次方程式を高速に解きます。\n"
    "【書式】\n構文:linsolve(式,変)\n"
    "【例】\n例: linsolve(連立式)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "zeros",
    "【機能】\n実数の範囲で式の零点(解)をリストで返します。\n"
    "【書式】\n構文: zeros(式, 変数)\n"
    "【例】\n例: zeros(x^2-4, x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "czeros",
    "【機能】\n複素数の範囲で式の零点(解)をリストで返します。\n"
    "【書式】\n構文: czeros(式,変数)\n"
    "【例】\n例: czeros(x^2+4, x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 微分・積分・極限・級数 ===
  { "diff",
    "【機能】\n式の導関数・高階微分・偏微分を求めます。\n"
    "【書式】\n構文: diff(式, 変数)\n構文: diff(式,変数,階)\n"
    "【例】\n例: diff(sin(x^2),x)\n例: diff(exp(x),x,2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "integrate",
    "【機能】\n不定積分(原始関数)または定積分を記号的に求めます。\n"
    "【書式】\n構文: integrate(式,変)\n構文:\n構文:int(式,x,a,b)\n"
    "【例】\n例: integrate(x^2, x)\n例: int(sin(x),x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "int",
    "【機能】\nintegrate の短縮名。不定積分・定積分を計算します。\n"
    "【書式】\n構文: int(式,変[,a,b])\n"
    "【例】\n例: int(x*exp(x), x)\n例: int(cos(x),x,0,1)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "limit",
    "【機能】\n変数→点の極限や片側極限を計算します。\n"
    "【書式】\n構文: limit(式,変数,点)\n構文: limit(式,x,点,側)\n"
    "【例】\n例: limit(sin(x)/x,x,0)\n例: limit(1/x, x, inf)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "series",
    "【機能】\n指定点でのテイラー・ローラン展開を求めます。\n"
    "【書式】\n構文:series(式,x=点,次)\n"
    "【例】\n例: series(sin(x),x=0)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "taylor",
    "【機能】\n剰余項を除くテイラー多項式を求めます。\n"
    "【書式】\n構文:taylor(式,x=点,次)\n"
    "【例】\n例: taylor(exp(x),x=0)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "sum",
    "【機能】\n離散総和 Σ や記号和を計算します。\n"
    "【書式】\n構文: sum(式,変数,a,b)\n"
    "【例】\n例: sum(k^2, k, 1, n)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "product",
    "【機能】\n総乗 Π や階乗積を計算します。\n"
    "【書式】\n構文:product(式,変,a,b)\n"
    "【例】\n例: product(k, k, 1, 5)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "desolve",
    "【機能】\n常微分方程式を記号的に解きます(初期値条件も指定可)。\n"
    "【書式】\n構文: desolve(式,x,y)\n"
    "【例】\n例: desolve(y'+y=0,x,y)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "laplace",
    "【機能】\n関数のラプラス変換を計算します。\n"
    "【書式】\n構文: laplace(式,t,s)\n"
    "【例】\n例: laplace(sin(t),t,s)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "ilaplace",
    "【機能】\nラプラス逆変換を計算し時間領域の関数へ戻します。\n"
    "【書式】\n構文: ilaplace(式,s,t)\n"
    "【例】\n例:ilaplace(1/s,s,t)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "grad",
    "【機能】\n多変数スカラー関数の勾配ベクトル ∇f を求めます。\n"
    "【書式】\n構文: grad(式, [x,y,z])\n"
    "【例】\n例: grad(x^2*y,[x,y])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "curl",
    "【機能】\n3次元ベクトル場の回転 rot を求めます。\n"
    "【書式】\n構文: curl(F, [x,y,z])\n"
    "【例】\n例: curl([-y,x,0],座標)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "divergence",
    "【機能】\nベクトル場の発散 div を求めます。\n"
    "【書式】\n構文:divergence(V,変数)\n"
    "【例】\n例:div計算([x,y])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "laplacian",
    "【機能】\n関数のラプラシアン Δf を求めます。\n"
    "【書式】\n構文:laplacian(式,変数)\n"
    "【例】\n例:laplacian(x^2,x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 整数・数論 ===
  { "ifactor",
    "【機能】\n整数を素因数分解します。\n"
    "【書式】\n構文: ifactor(整数)\n"
    "【例】\n例: ifactor(123456)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "ifactors",
    "【機能】\n素因数と指数のリスト [p1,e1,p2,e2,...] を返します。\n"
    "【書式】\n構文: ifactors(整数)\n"
    "【例】\n例: ifactors(360)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "isprime",
    "【機能】\n整数が素数であれば 1、合成数であれば 0 を返します。\n"
    "【書式】\n構文: isprime(整数)\n"
    "【例】\n例: isprime(1000003)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "nextprime",
    "【機能】\n指定整数より真に大きい最小の素数を返します。\n"
    "【書式】\n構文: nextprime(整数)\n"
    "【例】\n例: nextprime(100)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "prevprime",
    "【機能】\n指定整数より真に小さい最大の素数を返します。\n"
    "【書式】\n構文: prevprime(整数)\n"
    "【例】\n例: prevprime(100)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "gcd",
    "【機能】\n整数または多項式の最大公約数(GCD)を求めます。\n"
    "【書式】\n構文: gcd(a, b)\n"
    "【例】\n例: gcd(48, 180)\n例: gcd(x^2-1, x-1)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "lcm",
    "【機能】\n整数または多項式の最小公倍数(LCM)を求めます。\n"
    "【書式】\n構文: lcm(a, b)\n"
    "【例】\n例: lcm(12, 18)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "iegcd",
    "【機能】\n拡張ユークリッド互除法: a*u+b*v=gcd(a,b) の [u,v,d] を返す。\n"
    "【書式】\n構文: iegcd(a, b)\n"
    "【例】\n例: iegcd(35, 15)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "powmod",
    "【機能】\nべき乗剰余 (a^n mod m) を高速に計算します。\n"
    "【書式】\n構文: powmod(a, n, m)\n"
    "【例】\n例: powmod(3, 100, 7)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "mod",
    "【機能】\n整数 a を m で割った余り(剰余)を返します。\n"
    "【書式】\n構文: a mod m\n"
    "【例】\n例: 100 mod 7\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "irem",
    "【機能】\n整数除算の余りを返します。\n"
    "【書式】\n構文: irem(a, b)\n"
    "【例】\n例: irem(29, 6)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "iquo",
    "【機能】\n整数除算の商を返します。\n"
    "【書式】\n構文: iquo(a, b)\n"
    "【例】\n例: iquo(29, 6)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "euler",
    "【機能】\nオイラーのトーシェント関数 φ(n) を求めます。\n"
    "【書式】\n構文: euler(整数)\n"
    "【例】\n例: euler(12)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "divis",
    "【機能】\n正の整数・式のすべての約数をリストで返します。\n"
    "【書式】\n構文: divis(整数)\n"
    "【例】\n例: divis(24)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 線形代数・行列 ===
  { "matrix",
    "【機能】\nサイズと要素生成式から行列を作成します。\n"
    "【書式】\n構文: matrix(行,列,式)\n"
    "【例】\n例: matrix(2,2,式)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "det",
    "【機能】\n正方行列の行列式(数値・記号両対応)を求めます。\n"
    "【書式】\n構文: det(行列)\n"
    "【例】\n例: det(正方行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "inv",
    "【機能】\n正則行列の逆行列を求めます。\n"
    "【書式】\n構文: inv(行列)\n"
    "【例】\n例: inv(正方行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "rref",
    "【機能】\n行列の行簡約階段形(ガウス・ジョルダン法)を求めます。\n"
    "【書式】\n構文: rref(行列)\n"
    "【例】\n例: rref(拡大行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "tran",
    "【機能】\n行列の転置行列を返します。\n"
    "【書式】\n構文: tran(行列)\n"
    "【例】\n例: tran([[1,2]])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "egvl",
    "【機能】\n正方行列の固有値を求めます。\n"
    "【書式】\n構文: egvl(行列)\n"
    "【例】\n例: egvl(正方行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "egv",
    "【機能】\n正方行列の固有ベクトル行列を求めます。\n"
    "【書式】\n構文: egv(行列)\n"
    "【例】\n例: egv(正方行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "eigenvals",
    "【機能】\n正方行列の固有値のリストを返します。\n"
    "【書式】\n構文: eigenvals(行列)\n"
    "【例】\n例: eigenvals(行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "rank",
    "【機能】\n行列の階数(ランク)を求めます。\n"
    "【書式】\n構文: rank(行列)\n"
    "【例】\n例: rank(行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "trace",
    "【機能】\n正方行列のトレース(対角成分の和)を求めます。\n"
    "【書式】\n構文: trace(行列)\n"
    "【例】\n例: trace(正方行列)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "idn",
    "【機能】\nn×n の単位行列を生成します。\n"
    "【書式】\n構文: idn(サイズ)\n"
    "【例】\n例: idn(3)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "ranm",
    "【機能】\nランダム要素を持つ m×n 行列を生成します。\n"
    "【書式】\n構文:ranm(行,列[,範囲])\n"
    "【例】\n例: ranm(2, 3)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "matpow",
    "【機能】\n正方行列のべき乗 A^n (記号指数 n も可)を求めます。\n"
    "【書式】\n構文: matpow(行列,指数)\n"
    "【例】\n例: matpow(行列, n)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "dot",
    "【機能】\n2つのベクトルの内積を計算します。\n"
    "【書式】\n構文: dot(u, v)\n"
    "【例】\n例: dot([1,2],[3,4])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "cross",
    "【機能】\n2つの3次元ベクトルの外積を計算します。\n"
    "【書式】\n構文: cross(u, v)\n"
    "【例】\n例: cross(u, v) と外積\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 複素数 ===
  { "abs",
    "【機能】\n実数または複素数の絶対値、ベクトルのノルムを返します。\n"
    "【書式】\n構文: abs(z)\n"
    "【例】\n例: abs(3+4*i)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "arg",
    "【機能】\n複素数の偏角(ラジアンの -π〜π)を返します。\n"
    "【書式】\n構文: arg(z)\n"
    "【例】\n例: arg(1+i)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "re",
    "【機能】\n複素数の実部を取り出します。\n"
    "【書式】\n構文: re(z)\n"
    "【例】\n例: re(3+4*i)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "im",
    "【機能】\n複素数の虚部を取り出します。\n"
    "【書式】\n構文: im(z)\n"
    "【例】\n例: im(3+4*i)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "conj",
    "【機能】\n複素共役 (x - y*i) を返します。\n"
    "【書式】\n構文: conj(z)\n"
    "【例】\n例: conj(2+3*i)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 実数・丸め ===
  { "exact",
    "【機能】\n小数を正確な有理数(分数)表現に変換します。\n"
    "【書式】\n構文: exact(数値)\n"
    "【例】\n例: exact(0.125)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "approx",
    "【機能】\n記号式や分数を浮動小数近似値(evalf相当)にします。\n"
    "【書式】\n構文: approx(式)\n構文: approx(式,桁数)\n"
    "【例】\n例: approx(pi, 20)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "evalf",
    "【機能】\n正確な数式を指定桁数の浮動小数へ数値化します。\n"
    "【書式】\n構文: evalf(式[,桁数])\n"
    "【例】\n例: evalf(sqrt(2))\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "floor",
    "【機能】\n床関数: x 以下の最大の整数を返します。\n"
    "【書式】\n構文: floor(x)\n"
    "【例】\n例: floor(3.7)\n例: floor(-1.2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "ceil",
    "【機能】\n天井関数: x 以上の最小の整数を返します。\n"
    "【書式】\n構文: ceil(x)\n"
    "【例】\n例: ceil(3.2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "round",
    "【機能】\n数値を最も近い整数または指定小数桁に四捨五入します。\n"
    "【書式】\n構文: round(x[,桁数])\n"
    "【例】\n例: round(3.14159,2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "sign",
    "【機能】\n符号関数: 正なら +1、負なら -1、0 なら 0 を返します。\n"
    "【書式】\n構文: sign(x)\n"
    "【例】\n例: sign(-5)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "max",
    "【機能】\n引数またはリスト要素の最大値を返します。\n"
    "【書式】\n構文: max(a, b, ...)\n"
    "【例】\n例: max(3, 7, 2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "min",
    "【機能】\n引数またはリスト要素の最小値を返します。\n"
    "【書式】\n構文: min(a, b, ...)\n"
    "【例】\n例: min(3, 7, 2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 式書き換え (三角/指数対数) ===
  { "lin",
    "【機能】\n三角・双曲関数の積やべき乗を1次式の和へ線形化します。\n"
    "【書式】\n構文: lin(式)\n"
    "【例】\n例: lin(sin(x)^2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "tlin",
    "【機能】\n三角関数の積を和へ変換します(和積・倍角の整理)。\n"
    "【書式】\n構文: tlin(式)\n"
    "【例】\n例: tlin(sin*cos)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "texpand",
    "【機能】\n加法定理・倍角公式などで三角・指数関数を展開します。\n"
    "【書式】\n構文: texpand(式)\n"
    "【例】\n例:texpand(sin(2x))\n例:texpand(sin(a+b))\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "tsimplify",
    "【機能】\n三角・双曲関数を含む式を指数変換経由で簡約します。\n"
    "【書式】\n構文: tsimplify(式)\n"
    "【例】\n例: tsimplify(三角式)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "trig2exp",
    "【機能】\n三角関数 sin/cos 等を複素指数関数へ書き換えます。\n"
    "【書式】\n構文: trig2exp(式)\n"
    "【例】\n例: trig2exp(cos(x))\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "exp2trig",
    "【機能】\n複素指数関数を sin / cos の三角関数表現へ戻します。\n"
    "【書式】\n構文: exp2trig(式)\n"
    "【例】\n例:exp2trig(e^(ix))\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "lncollect",
    "【機能】\n複数の対数の和・差を1つの対数にまとめます。\n"
    "【書式】\n構文: lncollect(式)\n"
    "【例】\n例: lncollect(対数)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "exp2pow",
    "【機能】\nexp(b*ln(a)) を累乗の形 a^b に変換します。\n"
    "【書式】\n構文: exp2pow(式)\n"
    "【例】\n例: exp2pow(指数式)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === 確率・統計 ===
  { "comb",
    "【機能】\n組み合わせ数 nCk を計算します。\n"
    "【書式】\n構文: comb(n, k)\n"
    "【例】\n例: comb(10, 3)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "perm",
    "【機能】\n順列数 nPk を計算します。\n"
    "【書式】\n構文: perm(n, k)\n"
    "【例】\n例: perm(10, 3)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "rand",
    "【機能】\n一様乱数を生成します(引数なしで 0〜1 実数)。\n"
    "【書式】\n構文: rand([n])\n構文: rand(a..b)\n"
    "【例】\n例: rand(1..6)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "normald",
    "【機能】\n正規分布 N(μ, σ) の確率密度関数を計算します。\n"
    "【書式】\n構文: normald(μ,σ,x)\n"
    "【例】\n例: normald(0, 1, 0)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "normald_cdf",
    "【機能】\n正規分布の累積分布関数 P(X <= x) を計算します。\n"
    "【書式】\n構文: normald_cdf(x)\n"
    "【例】\n例: normald_cdf(1.96)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "binomial",
    "【機能】\n二項分布 B(n, p) の確率 P(X = k) を求めます。\n"
    "【書式】\n構文: binomial(n,p,k)\n"
    "【例】\n例: binomial(n,p,k)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "mean",
    "【機能】\nリストまたは行列各列の算術平均を返します。\n"
    "【書式】\n構文: mean(リスト)\n"
    "【例】\n例: mean([1,2,3,4,5])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "stddev",
    "【機能】\nデータの標準偏差(母標準偏差)を返します。\n"
    "【書式】\n構文: stddev(リスト)\n"
    "【例】\n例: stddev([1,2,3,4])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "median",
    "【機能】\nデータリストの中央値(メディアン)を返します。\n"
    "【書式】\n構文: median(リスト)\n"
    "【例】\n例: median([1,9,2,5])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === グラフ・2Dプロット ===
  { "plot",
    "【機能】\n1変数関数の2Dグラフを描画します。\n"
    "【書式】\n構文: plot(式[,範囲])\n"
    "【例】\n例: plot(sin(x), x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "plotparam",
    "【機能】\n媒介変数曲線 [x(t), y(t)] を描画します。\n"
    "【書式】\n構文: plotparam(P,t)\n"
    "【例】\n例:\n例: plotparam(座標)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "plotpolar",
    "【機能】\n極座標曲線 r = f(θ) を描画します。\n"
    "【書式】\n構文: plotpolar(r,t)\n"
    "【例】\n例: plotpolar(1+t, t)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "plotseq",
    "【機能】\n漸化式 u_{n+1}=f(u_n) のクモの巣図を描画します。\n"
    "【書式】\n構文: plotseq(式,x=u0)\n"
    "【例】\n例: plotseq(cos,x=1)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "plotfield",
    "【機能】\n微分方程式 y'=f(x,y) の方向場を描画します。\n"
    "【書式】\n構文: plotfield(f,X)\n"
    "【例】\n例: plotfield(式,X)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "plotode",
    "【機能】\n微分方程式の数値積分曲線(解曲線)を描画します。\n"
    "【書式】\n構文:plotode(f,X,x0)\n"
    "【例】\n例: plotode(y,座標)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "histogram",
    "【機能】\n度数分布のヒストグラムを描画します。\n"
    "【書式】\n構文:\n・histogram(値,下,幅)\n"
    "【例】\n例: histogram(データ)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === ジオメトリ ===
  { "point",
    "【機能】\n2D または 3D 空間上の点を定義します。\n"
    "【書式】\n構文: point(x,y[,z])\n"
    "【例】\n例: point(1, 2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "line",
    "【機能】\n2点を通る直線または方程式の直線を描画します。\n"
    "【書式】\n構文: line(点A, 点B)\n構文: line(ax+by=c)\n"
    "【例】\n例: line(y=2*x+1)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "circle",
    "【機能】\n中心と半径または直径の両端から円を定義します。\n"
    "【書式】\n構文: circle(中心,半径)\n"
    "【例】\n例: circle([0,0], 2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "distance",
    "【機能】\n2点や点と直線の距離を計算します。\n"
    "【書式】\n構文: distance(図1,図2)\n"
    "【例】\n例: distance(点A,B)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "inter",
    "【機能】\n2つの図形(直線や円など)の交点を求めます。\n"
    "【書式】\n構文:inter(図1,図2)\n"
    "【例】\n例: inter(直線1,直線2)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },

  // === リスト・基本制御 ===
  { "seq",
    "【機能】\n一般項の式から数列・リストを生成します。\n"
    "【書式】\n構文: seq(式,変数,a,b)\n"
    "【例】\n例: seq(k^2, k, 1, 5)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "makelist",
    "【機能】\n関数または初期値を反復適用してリストを作ります。\n"
    "【書式】\n構文: makelist(f,a,b)\n"
    "【例】\n例: makelist(f,1,5)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "range",
    "【機能】\n連続整数のリスト [0, 1, ..., n-1] を生成します。\n"
    "【書式】\n構文: range(n)\n構文: range(開始,終了)\n"
    "【例】\n例: range(1, 6)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "len",
    "【機能】\nリスト・文字列・ベクトルの要素数(長さ)を返します。\n"
    "【書式】\n構文: len(対象)\n"
    "【例】\n例: len([10,20,30])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "size",
    "【機能】\nリストや文字列の長さ、オブジェクトの次元を返します。\n"
    "【書式】\n構文: size(対象)\n"
    "【例】\n例: size(\"KhyCAS\")\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "sort",
    "【機能】\nリストの要素を昇順に並べ替えます。\n"
    "【書式】\n構文: sort(リスト)\n"
    "【例】\n例: sort([5,1,4,2])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "apply",
    "【機能】\nリストの全要素に指定関数を一括適用します(map相当)。\n"
    "【書式】\n構文: apply(関数,配列)\n"
    "【例】\n例: apply(sin,[0])\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "purge",
    "【機能】\nユーザー変数に代入された値を消去し文字変数に戻します。\n"
    "【書式】\n構文: purge(変数名)\n"
    "【例】\n例: purge(a)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "assume",
    "【機能】\n変数に実数範囲や正負などの仮定条件を付与します。\n"
    "【書式】\n構文: assume(x > 0)\n"
    "【例】\n例: assume(x > 0)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "about",
    "【機能】\n変数に現在設定されている仮定条件を表示します。\n"
    "【書式】\n構文: about(変数)\n"
    "【例】\n例: about(x)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "mksa",
    "【機能】\n単位付き数量を基本 SI 単位(m, kg, s, A等)に換算します。\n"
    "【書式】\n構文: mksa(数量_単位)\n"
    "【例】\n例: mksa(100_km/h)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "usimplify",
    "【機能】\n単位を含む式の次元と単位系を簡単化します。\n"
    "【書式】\n構文: usimplify(式)\n"
    "【例】\n例: usimplify(1_km)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
  { "convert",
    "【機能】\n単位変換や座標系・基数・式形式の変換を行います。\n"
    "【書式】\n構文: convert(値,形式)\n"
    "【例】\n例: convert(1_m,_cm)\n"
    "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る" },
};

static const char * findCommandJaBody(const char * cmdName) {
  if (!cmdName || !cmdName[0]) return nullptr;
  char clean[64];
  size_t i = 0;
  for (; i + 1 < sizeof(clean) && cmdName[i] != '\0'; ++i) {
    char c = cmdName[i];
    if (c == '(' || c == ' ' || c == '\t' || c == ':' || c == '\n') break;
    clean[i] = c;
  }
  clean[i] = '\0';
  const size_t n = sizeof(kCommandHelp) / sizeof(kCommandHelp[0]);
  for (size_t k = 0; k < n; ++k) {
    if (strcmp(clean, kCommandHelp[k].name) == 0) {
      return kCommandHelp[k].jaBody;
    }
  }
  return nullptr;
}

static const char * mapCategoryToJa(const char * categoryLabel) {
  if (!categoryLabel || !categoryLabel[0]) return "組込みコマンド";
  if (strstr(categoryLabel, "All")) return "すべて (全コマンド)";
  if (strstr(categoryLabel, "Options")) return "オプション";
  if (strstr(categoryLabel, "Algebra")) return "代数式変形";
  if (strstr(categoryLabel, "Calculus")) return "微分・積分";
  if (strstr(categoryLabel, "Complex")) return "複素数計算";
  if (strstr(categoryLabel, "2D") || strstr(categoryLabel, "Curves") || strstr(categoryLabel, "graphic")) return "2Dグラフ描画";
  if (strstr(categoryLabel, "Real")) return "実数・丸め";
  if (strstr(categoryLabel, "Arithmetic") || strstr(categoryLabel, "Integer")) return "整数・数論";
  if (strstr(categoryLabel, "Lin")) return "線形代数";
  if (strstr(categoryLabel, "Poly")) return "多項式操作";
  if (strstr(categoryLabel, "Prob")) return "確率・分布";
  if (strstr(categoryLabel, "Prg") || strstr(categoryLabel, "Prog")) return "プログラミング";
  if (strstr(categoryLabel, "Rewrite")) return "式書き換え";
  if (strstr(categoryLabel, "Solve") || strstr(categoryLabel, "Eq")) return "方程式求解";
  if (strstr(categoryLabel, "Matrix")) return "行列・ベクトル";
  if (strstr(categoryLabel, "Trig")) return "三角関数";
  if (strstr(categoryLabel, "FFT") || strstr(categoryLabel, "Signal")) return "信号・FFT";
  if (strstr(categoryLabel, "Stat") || strstr(categoryLabel, "List")) return "統計・リスト";
  if (strstr(categoryLabel, "String")) return "文字列・変換";
  if (strstr(categoryLabel, "Unit") || strstr(categoryLabel, "Phys")) return "物理定数・単位";
  if (strstr(categoryLabel, "Geom")) return "ジオメトリ";
  return "組込みコマンド";
}

} // namespace HelpJa
} // namespace KhyCAS

extern "C" {

void khycas_show_category_help(int categoryId, const char * /*categoryLabel*/) {
  using namespace KhyCAS::HelpJa;
  const size_t n = sizeof(kCategoryHelp) / sizeof(kCategoryHelp[0]);
  for (size_t i = 0; i < n; ++i) {
    if (kCategoryHelp[i].id == categoryId) {
      BrainNotifyCustomHelp(kCategoryHelp[i].title, kCategoryHelp[i].body);
      return;
    }
  }
  BrainNotifyCustomHelp("コマンド一覧",
    "【機能】\nコマンド分類を選択して目的の関数を探せます。\n"
    "【操作】\n[→/Ent]開く [Esc]戻る");
}

void khycas_show_command_help(const char * cmdName,
                                     const char * /*enDesc*/,
                                     const char * example1,
                                     const char * /*example2*/,
                                     const char * categoryLabel) {
  using namespace KhyCAS::HelpJa;
  char title[64];
  snprintf(title, sizeof(title), "%s", (cmdName && cmdName[0]) ? cmdName : "コマンド詳細");
  const char * jaBody = findCommandJaBody(cmdName);
  if (jaBody) {
    BrainNotifyCustomHelp(title, jaBody);
    return;
  }

  // 未収録コマンドも統一フォーマット・日本語のみで表示（英語原文は非表示）
  char buf[768];
  const char * catJa = mapCategoryToJa(categoryLabel);
  size_t off = 0;
  off += static_cast<size_t>(snprintf(
      buf + off, sizeof(buf) - off,
      "【機能】\n%s分類の組込みCASコマンドです。\n詳細な解説は[F1]で参照できます。\n",
      catJa));

  // 例が144pxに収まる短い文字列の場合のみ、標準サイズ日本語プレフィックス付きで表示
  if (example1 && example1[0] && strlen(example1) <= 15 &&
      strchr(example1, '\n') == nullptr && off < sizeof(buf) - 120) {
    off += static_cast<size_t>(snprintf(
        buf + off, sizeof(buf) - off,
        "【例】\n例: %s\n",
        example1));
  }

  snprintf(buf + off, sizeof(buf) - off,
           "【操作】\n[F3]挿入  [Ent]例1\n[F1]詳細  [Esc]戻る");
  BrainNotifyCustomHelp(title, buf);
}

void khycas_show_var_help(const char * itemText) {
  char title[64];
  snprintf(title, sizeof(title), "%s", (itemText && itemText[0]) ? itemText : "変数一覧");
  BrainNotifyCustomHelp(title,
    "【機能】\n定義済みの変数・ユーザー関数を選択して式に挿入します。\n"
    "【書式】\n構文: a:=式 で定義\n構文: purge(a) で削除\n"
    "【操作】\n[F3/Ent]式へ挿入\n[←/Esc]閉じる");
}

void khycas_show_menu_help(const char * itemText) {
  if (!itemText || !itemText[0]) {
    BrainNotifyCustomHelp("メニュー",
      "【機能】\nシェル管理・セッション保存/読込・環境設定を開きます。\n"
      "【操作】\n[F3/Ent]実行 [Esc]閉じる");
    return;
  }
  const char * jaTitle = "KhyCAS設定";
  const char * desc = "【機能】\nKhyCAS メニュー項目を実行・開きます。\n【操作】\n[F3/Ent]実行 [Esc]閉じる";
  if (strstr(itemText, "Applications")) {
    jaTitle = "アプリ一覧";
    desc = "【機能】\nテキスト編集・図形・スプレッド等のサブ機能へ切替。\n【操作】\n[F3/Ent]開く [Esc]閉じる";
  } else if (strstr(itemText, "Save session as")) {
    jaTitle = "別名で保存";
    desc = "【機能】\n現在の計算セッションに新しい名前を付けて保存します。\n【操作】\n[F3/Ent]保存 [Esc]閉じる";
  } else if (strstr(itemText, "Save") || strstr(itemText, "Sauv")) {
    jaTitle = "セッション保存";
    desc = "【機能】\n現在の計算セッションやスクリプトを保存します。\n【操作】\n[F3/Ent]保存 [Esc]閉じる";
  } else if (strstr(itemText, "Load") || strstr(itemText, "Open") || strstr(itemText, "Charg")) {
    jaTitle = "開く・読込";
    desc = "【機能】\n保存済みのセッションやスクリプトを開いて読み込みます。\n【操作】\n[F3/Ent]開く [Esc]閉じる";
  } else if (strstr(itemText, "New")) {
    jaTitle = "新規作成";
    desc = "【機能】\n新しい計算セッションまたはスクリプトを作成します。\n【操作】\n[F3/Ent]作成 [Esc]閉じる";
  } else if (strstr(itemText, "Run")) {
    jaTitle = "スクリプト実行";
    desc = "【機能】\n読込済みセッションやスクリプトを順番に実行します。\n【操作】\n[F3/Ent]実行 [Esc]閉じる";
  } else if (strstr(itemText, "Script editor") || strstr(itemText, "Edit")) {
    jaTitle = "コード編集";
    desc = "【機能】\nXcas / Python スクリプトエディタ画面を開きます。\n【操作】\n[F3/Ent]開く [Esc]閉じる";
  } else if (strstr(itemText, "Clear history")) {
    jaTitle = "履歴を消去";
    desc = "【機能】\nシェルの計算履歴と定義済み変数をすべて消去します。\n【操作】\n[F3/Ent]実行 [Esc]閉じる";
  } else if (strstr(itemText, "Clear") || strstr(itemText, "Effac")) {
    jaTitle = "内容消去";
    desc = "【機能】\nスクリプトまたは選択内容を消去します。\n【操作】\n[F3/Ent]実行 [Esc]閉じる";
  } else if (strstr(itemText, "Config") || strstr(itemText, "Setting") || strstr(itemText, "Param")) {
    jaTitle = "環境設定";
    desc = "【機能】\n角度単位(Deg/Rad)・複素数モード・表示桁数などを設定。\n【操作】\n[F3/Ent]開く [Esc]閉じる";
  } else if (strstr(itemText, "Undo") || strstr(itemText, "Annul")) {
    jaTitle = "元に戻す";
    desc = "【機能】\n直前の操作や入力を取り消して1つ前の状態に戻します。\n【操作】\n[F3/Ent]実行 [Esc]閉じる";
  } else if (strstr(itemText, "Copy") || strstr(itemText, "Copi") || strstr(itemText, "Clip")) {
    jaTitle = "コピー";
    desc = "【機能】\n選択中の式または計算結果をコピーして再利用できます。\n【操作】\n[F3/Ent]実行 [Esc]閉じる";
  } else if (strstr(itemText, "Python") || strstr(itemText, "MicroPy")) {
    jaTitle = "言語切替";
    desc = "【機能】\nインタプリタを Xcas(CAS) / Python 等に切り替えます。\n【操作】\n[F3/Ent]切替 [Esc]閉じる";
  } else if (strstr(itemText, "Help") || strstr(itemText, "Aide")) {
    jaTitle = "ヘルプ表示";
    desc = "【機能】\nコマンドの構文説明・使用例ヘルプ画面を開きます。\n【操作】\n[F3/Ent]開く [Esc]閉じる";
  }
  BrainNotifyCustomHelp(jaTitle, desc);
}

void khycas_clear_help() {
  BrainClearCustomHelp();
}

} // extern "C"

#endif // APPS_KHYCAS_KHYCAS_HELP_JA_H
