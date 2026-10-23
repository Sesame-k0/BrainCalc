#ifndef ION_SIMULATOR_BRAIN_HELP_DATA_H
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
  { I18n::Message::DiffCommandWithArg, L"微分係数", L"f(x)の x=a における\n微分係数を計算します。\n\n【書式】\ndiff(式, 変数, 値)\n\n【例】\ndiff(x^2, x, 3) → 6" },
  { I18n::Message::IntCommandWithArg, L"定積分", L"区間[a, b]における\n関数の定積分を\n数値計算します。\n\n【書式】\nint(式, 変数, a, b)\n\n【例】\nint(x^2, x, 0, 1)\n→ 1/3" },
  { I18n::Message::SumCommandWithArg, L"総和 (シグマ)", L"n=a から b までの\nf(n) の総和を\n計算します。\n\n【書式】\nsum(式, 変数, a, b)\n\n【例】\nsum(n, n, 1, 10)\n→ 55" },
  { I18n::Message::ProductCommandWithArg, L"総乗 (プロダクト)", L"n=a から b までの\nf(n) の掛け算の総積\nを計算します。\n\n【書式】\nprod(式, 変数, a, b)\n\n【例】\nprod(n, n, 1, 5)\n→ 120" },
  { I18n::Message::Infinity, L"無限大", L"極限や定積分の区間\nなどに使用する\n正の無限大記号です。\n\n【例】\nint(1/x^2, x, 1, inf)\n→ 1" },
  { I18n::Message::AbsCommandWithArg, L"絶対値 / 大きさ", L"実数の絶対値、または\n複素数 z の大きさ\n|z| を計算します。\n\n【例】\nabs(3+4i) → 5" },
  { I18n::Message::ArgCommandWithArg, L"偏角 (ラジアン)", L"複素数 z の偏角\nθ = arg(z) を\n計算します。\n\n【例】\narg(1+i) → π/4" },
  { I18n::Message::ReCommandWithArg, L"実部", L"複素数 z = a + bi の\n実部 a を取り出します。\n\n【例】\nre(3+4i) → 3" },
  { I18n::Message::ImCommandWithArg, L"虚部", L"複素数 z = a + bi の\n虚部 b を取り出します。\n\n【例】\nim(3+4i) → 4" },
  { I18n::Message::ConjCommandWithArg, L"共役複素数", L"複素数 z = a + bi の\n共役複素数 a - bi を\n計算します。\n\n【例】\nconj(3+4i) → 3-4i" },
  { I18n::Message::BinomialCommandWithArg, L"組合せ (nCr)", L"異なる n 個から k 個を\n選ぶ組合せの数\nnCk を計算します。\n\n【書式】\nbinomial(n, k)\n\n【例】\nbinomial(5, 2) → 10" },
  { I18n::Message::PermuteCommandWithArg, L"順列 (nPr)", L"異なる n 個から k 個を\n並べる順列の数\nnPr を計算します。\n\n【書式】\npermute(n, k)\n\n【例】\npermute(5, 2) → 20" },
  { I18n::Message::FactorialCommandWithArg, L"階乗", L"正の整数 n の階乗\nn! = n*(n-1)*...*1\nを計算します。\n\n【例】\n5! → 120" },
  { I18n::Message::NormCDFCommandWithArg, L"累積正規分布", L"平均 μ、標準偏差 σ の\n正規分布において、\n負の無限大から a までの\n累積確率を計算します。" },
  { I18n::Message::NormCDF2CommandWithArg, L"区間累積正規分布", L"平均 μ、標準偏差 σ の\n正規分布において、\n区間 [a, b] の\n確率 P(a≦X≦b) を\n計算します。" },
  { I18n::Message::InvNormCommandWithArg, L"逆正規分布", L"累積確率が p となる\n確率変数 x の境界値\nを計算します。" },
  { I18n::Message::NormPDFCommandWithArg, L"正規分布の確率密度", L"平均 μ、標準偏差 σ の\n正規分布における\nx の確率密度関数の値\nを計算します。" },
  { I18n::Message::BinomialPDFCommandWithArg, L"二項分布の確率", L"試行回数 n、確率 p の\n二項分布において、\n成功回数が丁度 m 回と\nなる確率を計算します。" },
  { I18n::Message::BinomialCDFCommandWithArg, L"累積二項分布", L"試行回数 n、確率 p の\n二項分布において、\n成功回数が 0 から m 回\n以下となる確率を計算します。" },
  { I18n::Message::InvBinomialCommandWithArg, L"逆二項分布", L"累積確率が α 以上と\nなる最小の成功回数 m\nを計算します。" },
  { I18n::Message::GcdCommandWithArg, L"最大公約数 (GCD)", L"2つ以上の整数の\n最大公約数を\n計算します。\n\n【例】\ngcd(24, 36) → 12" },
  { I18n::Message::LcmCommandWithArg, L"最小公倍数 (LCM)", L"2つ以上の整数の\n最小公倍数を\n計算します。\n\n【例】\nlcm(12, 18) → 36" },
  { I18n::Message::RemCommandWithArg, L"剰余 (余り)", L"整数 a を b で割った\nときの余り (a mod b)\nを計算します。\n\n【例】\nrem(17, 5) → 2" },
  { I18n::Message::QuoCommandWithArg, L"商 (整数の商)", L"整数 a を b で割った\nときの整数の商\n(切り捨て) を計算します。\n\n【例】\nquo(17, 5) → 3" },
  { I18n::Message::FracCommandWithArg, L"小数部分", L"実数 x の小数部分\n(x - floor(x)) を\n取り出します。\n\n【例】\nfrac(3.14) → 0.14" },
  { I18n::Message::FloorCommandWithArg, L"床関数 (ガウス記号)", L"実数 x 以下の最大の\n整数 [x] を計算します。\n\n【例】\nfloor(3.8) → 3\nfloor(-2.3) → -3" },
  { I18n::Message::CeilCommandWithArg, L"天井関数", L"実数 x 以上の最小の\n整数を計算します。\n\n【例】\nceil(3.2) → 4\nceil(-2.8) → -2" },
  { I18n::Message::RoundCommandWithArg, L"四捨五入", L"数 x を小数点以下\nn 桁に四捨五入します。\n\n【書式】\nround(x, 桁数)\n\n【例】\nround(3.14159, 2)\n→ 3.14" },
  { I18n::Message::DeterminantCommandWithArg, L"行列式 (det)", L"正方行列 M の\n行列式 det(M) を\n計算します。\n\n【例】\ndet([[1,2],[3,4]])\n→ -2" },
  { I18n::Message::InverseCommandWithArg, L"逆行列", L"正方行列 M の\n逆行列 M^-1 を\n計算します。\n\n(行列 M の -1 乗でも\n計算可能です)" },
  { I18n::Message::TransposeCommandWithArg, L"転置行列", L"行列 M の行と列を\n入れ替えた転置行列\n^t M を計算します。" },
  { I18n::Message::TraceCommandWithArg, L"トレース (対角和)", L"正方行列 M の\n主対角成分の総和\n(対角和) を計算します。" },
  { I18n::Message::DimensionCommandWithArg, L"行列・ベクトルの次元", L"行列 M の行数・列数、\nまたはベクトルの長さを\nリスト {行, 列} で\n返します。" },
  { I18n::Message::DotCommandWithArg, L"内積 (ドット積)", L"2つのベクトル u, v の\n内積 (スカラー積)\nu・v を計算します。" },
  { I18n::Message::CrossCommandWithArg, L"外積 (ベクトル積)", L"3次元ベクトル u, v の\n外積 (ベクトル積)\nu×v を計算します。" },
  { I18n::Message::NormVectorCommandWithArg, L"ベクトルのノルム", L"ベクトル v の幾何学的\nな長さ (ユークリッドノルム\n√(|v1|^2 + |v2|^2...))\nを計算します。" },
  { I18n::Message::RowEchelonFormCommandWithArg, L"行階段形 (REF)", L"行列 M を行基本変形\nによって行階段形に\n変換します。" },
  { I18n::Message::ReducedRowEchelonFormCommandWithArg, L"被約行階段形 (RREF)", L"連立方程式の解を求める\n掃き出し法（ガウス・ジョルダン法）\nを行い、被約行階段形に変換します。" },
  { I18n::Message::RandintCommandWithArg, L"整数の乱数", L"a 以上 b 以下の\nランダムな整数を\n1つ生成します。\n\n【書式】\nrandint(最小, 最大)\n\n【例】\nrandint(1, 6) → 4" },
  { I18n::Message::RandomCommandWithArg, L"実数の乱数 (0〜1)", L"区間 [0, 1) の一様な\n疑似乱数（実数）を\n1つ生成します。" },
};

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
