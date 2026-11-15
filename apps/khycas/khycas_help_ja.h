#ifndef KHYCAS_HELP_JA_H
#define KHYCAS_HELP_JA_H

#include <cstdio>
#include <cstring>

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);
extern "C" void BrainClearCustomHelp();

namespace {

struct CategoryHelpEntry {
  const char * title;
  const char * desc;
};

static const CategoryHelpEntry kCategoryHelp[24] = {
  { "すべて (All)",
    "【全コマンド一覧】\n"
    "KhyCASで使える全コマンドを辞書順に表示します。\n\n"
    "・[abc]モードで頭文字を打つと先頭一致ジャンプ\n"
    "・[PgUp/PgDn]: 頁送り\n"
    "・[F3 / → / Enter]: 開く\n"
    "・[Esc / ←]: シェルへ戻る" },
  { "代数式変形 (Algebra)",
    "【代数式の展開・整理】\n"
    "因数分解、展開、約分、部分分数分解、三角関数変形などのコマンド群です。\n\n"
    "代表例:\n"
    "・factor(x^4-1)\n"
    "・expand((x+1)^3)\n"
    "・simplify(...) / partfrac(...)" },
  { "線形代数 (Linear Alg)",
    "【行列・ベクトル・連立】\n"
    "行列式、逆行列、固有値・固有ベクトル、階段化(rref)、内積・外積など。\n\n"
    "代表例:\n"
    "・det([[1,2],[3,4]])\n"
    "・inv(A) / tran(A) / rref(A)\n"
    "・egvl(A) / egv(A)" },
  { "微分積分 (Calculus)",
    "【微積・極限・級数・和】\n"
    "記号微分、不定・定積分、極限、テイラー展開、総和、総積を扱います。\n\n"
    "代表例:\n"
    "・diff(sin(x),x)\n"
    "・integrate(1/(x^2+1),x)\n"
    "・limit(sin(x)/x,x,0)" },
  { "整数・暗号 (Arithmetic)",
    "【整数論・素数・剰余】\n"
    "素因数分解、素数判定、最大公約数、拡張ユークリッド、法べき乗など。\n\n"
    "代表例:\n"
    "・ifactor(2024) / isprime(97)\n"
    "・gcd(a,b) / lcm(a,b)\n"
    "・iquo / irem / powmod" },
  { "複素数 (Complexes)",
    "【複素数演算・方程式】\n"
    "絶対値、偏角、実部・虚部、共役複素数、複素数範囲での方程式解法など。\n\n"
    "代表例:\n"
    "・abs(z) / arg(z) / conj(z)\n"
    "・re(z) / im(z)\n"
    "・csolve(x^2+1=0,x)" },
  { "グラフ (Curves / Plot)",
    "【2D関数・媒介変数・極座標】\n"
    "関数グラフ、媒介変数曲線、極座標、数列、勾配場などの描画コマンド群。\n\n"
    "代表例:\n"
    "・plot(sin(x),x)\n"
    "・plotpolar(1+cos(t),t)\n"
    "・plotparam([cos(t),sin(t)],t)" },
  { "多項式 (Polynomials)",
    "【多項式の根・係数・除算】\n"
    "多項式の商・余り、数値根、係数抽出、終結式などを計算します。\n\n"
    "代表例:\n"
    "・proot(x^3-1) / pcoeff(...)\n"
    "・quo(P,Q,x) / rem(P,Q,x)\n"
    "・gcd(P,Q)" },
  { "確率・分布 (Probabilities)",
    "【場合の数・確率分布】\n"
    "順列、組合せ、二項分布、正規分布、ポアソン分布、乱数生成など。\n\n"
    "代表例:\n"
    "・comb(n,k) / perm(n,k)\n"
    "・normald / binomial\n"
    "・rand(n) / randnorm(m,s)" },
  { "プログラム命令 (Cmds)",
    "【制御構文・入出力】\n"
    "if/for/while文、関数定義、条件分岐やデバッグ用の命令一覧です。\n\n"
    "・[F3]: 命令名を挿入\n"
    "・[Enter]: 例1を挿入\n"
    "・[F1]: 詳細ヘルプ表示" },
  { "実数・端数処理 (Reals)",
    "【実数計算・丸め・比較】\n"
    "厳密値/近似値変換、切り捨て・切り上げ・四捨五入、符号、最大・最小。\n\n"
    "代表例:\n"
    "・exact(0.25) → 1/4\n"
    "・approx(pi) → 3.14159...\n"
    "・floor / ceil / round / min / max" },
  { "方程式解法 (Solve)",
    "【代数・微分・漸化式解法】\n"
    "方程式・連立方程式・常微分方程式・数値解・漸化式を解きます。\n\n"
    "代表例:\n"
    "・solve(x^2-3*x+2=0,x)\n"
    "・linsolve([x+y=3,x-y=1],[x,y])\n"
    "・desolve(y'=y,x,y) / fsolve" },
  { "統計・回帰 (Statistics)",
    "【記述統計・回帰グラフ】\n"
    "平均、標準偏差、分散、中央値、ヒストグラム、線形・多項式回帰分析など。\n\n"
    "代表例:\n"
    "・mean(L) / stddev(L)\n"
    "・histogram(L) / barplot(L)\n"
    "・linear_regression_plot(X,Y)" },
  { "三角・双曲線 (Trig)",
    "【三角関数・双曲線関数】\n"
    "sin/cos/tanと逆関数、双曲線関数、三角式の展開(texpand)や簡約(tlin)等。\n\n"
    "代表例:\n"
    "・sin(x) / asin(x) / sinh(x)\n"
    "・texpand / tlin / halftan" },
  { "表示オプション (Options)",
    "【グラフ色・ソルバ等の設定】\n"
    "plotやfsolve等に渡す色・線種・アルゴリズム指定用オプション一覧です。\n\n"
    "例: plot(sin(x),x,display=red)" },
  { "リスト操作 (Lists)",
    "【数列・配列の生成と加工】\n"
    "リスト生成(makelist, seq, range)、要素数、並び替え、写像(apply)等。\n\n"
    "代表例:\n"
    "・makelist(k^2,k,1,5)\n"
    "・seq(2*k,k,1,5) / range(5)\n"
    "・sort(L) / len(L)" },
  { "行列 (Matrices)",
    "【行列の生成・線形計算】\n"
    "対話入力での行列作成、単位行列、逆行列、トレース、対角化など。\n\n"
    "代表例:\n"
    "・matrix(2,2,(j,k)->j+k)\n"
    "・identity(3) / ranm(2,2)\n"
    "・det / inv / trace" },
  { "プログラム (Programs)",
    "【自作関数・論理演算】\n"
    "関数代入(:=)、条件文、真偽・ビット演算、デバッグコマンド等。\n\n"
    "例: f(x):=x^2+1" },
  { "変数管理 (Variables)",
    "【変数の代入・仮定・削除】\n"
    "変数の解放(purge)、仮定設定(assume, about)、セッションリセット(restart)等。\n\n"
    "※ シェルで[F9]を押すと変数一覧を直接開けます。" },
  { "物理定数 (Physics)",
    "【理学・物理学の定数一覧】\n"
    "光速 c_、プランク定数 h_、ボルツマン定数 k_、電子質量等の組み込み定数。" },
  { "物理単位 (Units)",
    "【SI単位・単位変換コマンド】\n"
    "長さ・質量・時間・エネルギー等の単位記号と単位換算(mksa, ufactor等)。" },
  { "2D幾何 (Geometry)",
    "【点・直線・円・図形描画】\n"
    "2次元座標上の点、線分、直線、円、多角形、角度や交点の計算と描画。\n\n"
    "代表例:\n"
    "・point(1,2) / line(A,B)\n"
    "・circle(point(0,0),2)" },
  { "3D立体幾何 (3D)",
    "【空間図形・曲面描画】\n"
    "空間上の点・平面・球・円柱・多面体や z=f(x,y) 3D曲面グラフを描画します。\n\n"
    "例: plot3d(x^2-y^2)" },
  { "タートルグラフィックス",
    "【Logoタートル命令】\n"
    "亀(タートル)を前進(avance)・回転(tourne_gauche)させて幾何図形を描きます。" }
};

struct CommandHelpEntry {
  const char * name;
  const char * desc;
};

static const CommandHelpEntry kCommandHelp[] = {
  { "factor", "【因数分解】\n多項式や有理式を因数分解します。\n引数: 式 (, 変数)\n例: factor(x^4-1)\n→ (x-1)*(x+1)*(x^2+1)" },
  { "cfactor", "【複素範囲で因数分解】\n複素数の範囲で多項式を因数分解します。\n引数: 式 (, 変数)\n例: cfactor(x^2+1)" },
  { "expand", "【式の展開】\n積やべき乗を展開して整理します。\n引数: 式\n例: expand((x+y)^3)" },
  { "simplify", "【式の簡単化】\n代数式・三角・指数式を可能な限り簡約します。\n引数: 式\n例: simplify((x^2-1)/(x-1))" },
  { "normal", "【正規形への整理】\n有理式を通分・展開し標準の分数形に整理します。\n引数: 式\n例: normal(1/(x-1)+1/(x+1))" },
  { "partfrac", "【部分分数分解】\n有理式を部分分数の和に分解します。\n引数: 式 (, 変数)\n例: partfrac(1/(x^2-1))" },
  { "tcollect", "【三角関数の積和整理】\n三角・指数関数の線形結合へまとめます。\n例: tcollect(sin(x)*cos(x))" },
  { "texpand", "【三角・指数関数の展開】\n倍角・加法定理や指数法則を展開します。\n例: texpand(sin(2*x))" },
  { "tlin", "【三角関数の線形化】\nsin/cosのべき乗や積を一次式の和に変換します。\n例: tlin(sin(x)^2)" },
  { "ratnormal", "【有理式の展開・約分】\n展開した分子/分母に整理し共通因数を約分します。" },
  { "subst", "【代入・置換】\n式の中の変数を別の式・数値で置き換えます。\n引数: 式, 変数=値\n例: subst(x^2+1, x=3) → 10" },
  { "solve", "【方程式を解く】\n実数(設定により複素)範囲で方程式・連立方程式を解きます。\n引数: 式または等式, 変数\n例: solve(x^2-a*x+2=0, x)" },
  { "csolve", "【複素数範囲で方程式を解く】\n複素解を含めて方程式を解きます。\n例: csolve(x^2+4=0, x) → [-2*i,2*i]" },
  { "fsolve", "【数値的に方程式を解く】\n非線形方程式の近似解を反復法で求めます。\n引数: 式, 変数=初期値\n例: fsolve(cos(x)=x, x=0.5)" },
  { "linsolve", "【連立一次方程式の解法】\n連立線形方程式を記号のまま解きます。\n例: linsolve([x+y=3,x-y=1],[x,y])" },
  { "desolve", "【常微分方程式の解法】\n微分方程式の一般解・初期値解を求めます。\n例: desolve(y'+2*y=exp(x), x, y)" },
  { "rsolve", "【漸化式を解く】\n数列の漸化式から一般項を求めます。\n例: rsolve(u(n+1)=2*u(n)+1, u(n), u(0)=1)" },
  { "odesolve", "【微分方程式の数値解法】\n微分方程式をルンゲクッタ等で数値積分します。\n例: odesolve(sin(x*y), [x,y], [0,1], 2)" },
  { "diff", "【記号微分 (導関数)】\n式の導関数・偏導関数・高階微分を求めます。\n引数: 式, 変数 (, 階数)\n例: diff(sin(x)*exp(x), x)" },
  { "integrate", "【不定積分・定積分】\n原始関数または定積分の値を求めます。\n不定: integrate(式, x)\n定積分: integrate(式, x, a, b)" },
  { "int", "【不定積分・定積分 (短縮形)】\nintegrate と同じ機能です。\n不定: int(1/(x^2+1), x)\n定積分: int(x^2, x, 0, 1)" },
  { "limit", "【極限値の計算】\n指定点・無限大への極限(片側極限可)を求めます。\n引数: 式, 変数, 極限点\n例: limit(sin(x)/x, x, 0) → 1" },
  { "series", "【テイラー/ローラン展開】\n指定点まわりのべき級数展開を求めます。\n引数: 式, 変数=点, 次数\n例: series(sin(x), x=0, 5)" },
  { "taylor", "【テイラー展開】\n指定点・次数でのテイラー展開を求めます。\n例: taylor(exp(x), x=0, 4)" },
  { "sum", "【総和 Σ の計算】\n離散和(記号和・数値和)を計算します。\n例: sum(k^2, k, 1, n)" },
  { "product", "【総積 Π の計算】\n範囲内の積を求めます。\n例: product(k, k, 1, 5) → 120" },
  { "matrix", "【行列の作成】\nリスト・要素数・生成式から行列を作成します。\n例: matrix(2,2,(j,k)->j+k)\n※ [[1,2],[3,4]] でも直接入力可能" },
  { "det", "【行列式】\n正方行列の行列式を求めます(文字式対応)。\n例: det([[a,b],[c,d]]) → a*d-b*c" },
  { "inv", "【逆行列】\n正則行列の逆行列を求めます。\n例: inv([[1,2],[3,4]])" },
  { "tran", "【転置行列】\n行列の行と列を入れ替えた転置行列を求めます。\n例: tran([[1,2],[3,4]])" },
  { "rref", "【行簡約階段形】\nガウス・ジョルダン法で行列を簡約階段形へ変形します。\n連立方程式やランク判定に便利です。" },
  { "rank", "【行列の階数 (ランク)】\n行列の線形独立な行(列)の個数を求めます。" },
  { "trace", "【行列のトレース】\n正方行列の対角成分の和を求めます。" },
  { "egvl", "【固有値 (Eigenvalues)】\n正方行列の固有値を求めます。\n例: egvl([[2,1],[1,2]])" },
  { "egv", "【固有ベクトル (Eigenvectors)】\n行列の固有ベクトル行列を求めます。" },
  { "matpow", "【行列のべき乗】\n正方行列の n 乗を計算します(記号 n 可)。\n例: matpow(A, n)" },
  { "identity", "【単位行列 idn】\nn次の単位行列を生成します。\n例: identity(3)" },
  { "ranm", "【ランダム行列】\nn×m の整数乱数行列を生成します。\n例: ranm(2, 3)" },
  { "cross", "【ベクトルの外積】\n3次元ベクトル u, v の外積 u×v を計算します。" },
  { "dot", "【ベクトルの内積】\n2つのベクトルの内積(ドット積)を計算します。" },
  { "ker", "【線形写像の核】\n行列 A の零空間 (Ax=0 の基底) を求めます。" },
  { "image", "【線形写像の像】\n行列 A の列空間(像)の基底を求めます。" },
  { "charpoly", "【固有多項式】\n行列の固有多項式 det(x*I - A) を求めます。" },
  { "ifactor", "【整数の素因数分解】\n整数を素数の積に分解します。\n例: ifactor(2024)" },
  { "isprime", "【素数判定】\n整数が素数であれば true(1)、合成数なら false(0)。\n例: isprime(997)" },
  { "nextprime", "【次の素数】\nn より大きい最小の素数を求めます。" },
  { "prevprime", "【前の素数】\nn より小さい最大の素数を求めます。" },
  { "gcd", "【最大公約数 / 最大公約式】\n整数・多項式の最大公約数(式)を求めます。\n例: gcd(24, 36) → 12" },
  { "lcm", "【最小公倍数 / 最小公倍式】\n整数・多項式の最小公倍数(式)を求めます。\n例: lcm(12, 18) → 36" },
  { "egcd", "【拡張ユークリッド互除法】\na*u + b*v = gcd(a,b) を満たす [u, v, d] を返します。" },
  { "iegcd", "【整数の拡張ユークリッド】\n整数の一次不定方程式の解 [u, v, gcd] を求めます。" },
  { "iquo", "【整数の商】\n整数 a を b で割った商を求めます。\n例: iquo(17, 5) → 3" },
  { "irem", "【整数の余り】\n整数 a を b で割った余りを求めます。\n例: irem(17, 5) → 2" },
  { "mod", "【剰余類・合同算術】\na % m として m を法とする剰余数を作成します。\n例: 17 mod 5 → 2 mod 5" },
  { "powmod", "【べき剰余 (a^n mod m)】\n大きなべき乗の余りを高速計算します。\n例: powmod(2, 100, 97)" },
  { "euler", "【オイラーのトーシェント関数 φ(n)】\n1..n で n と互いに素な整数の個数を返します。" },
  { "factorial", "【階乗 n!】\nn! を計算します。(数値は n! でも可)" },
  { "comb", "【組合せ nCk】\nn 個から k 個選ぶ組合せの総数を求めます。" },
  { "perm", "【順列 nPk】\nn 個から k 個並べる順列の総数を求めます。" },
  { "proot", "【多項式の数値根】\n係数リストまたは多項式の全複素根を数値計算します。\n例: proot(x^3-2*x-5)" },
  { "pcoeff", "【根から多項式を構成】\n根のリストから展開された係数を求めます。\n例: pcoeff([1, 2, 3])" },
  { "quo", "【多項式の商】\n多項式 P を Q で割った商を求めます。\n例: quo(x^3-1, x-1, x)" },
  { "rem", "【多項式の余り】\n多項式 P を Q で割った剰余を求めます。\n例: rem(x^3+2, x-1, x)" },
  { "resultant", "【終結式】\n2つの多項式が共通根を持つ条件(終結式)を計算します。" },
  { "roots", "【多項式の厳密な根と重複度】\n多項式の根と重複度のペアを返します。" },
  { "abs", "【絶対値】\n実数・複素数の絶対値 |z| を返します。\n例: abs(3+4*i) → 5" },
  { "arg", "【偏角 (Argument)】\n複素数 z の偏角をラジアン等で返します。\n例: arg(1+i) → pi/4" },
  { "re", "【実部 (Real part)】\n複素数の実部を取り出します。\n例: re(3+4*i) → 3" },
  { "im", "【虚部 (Imaginary part)】\n複素数の虚部を取り出します。\n例: im(3+4*i) → 4" },
  { "conj", "【共役複素数】\n複素数 a+b*i の共役 a-b*i を返します。" },
  { "exact", "【小数の有理数(分数)化】\n小数を近似分数・厳密値へ変換します。\n例: exact(0.125) → 1/8" },
  { "approx", "【浮動小数点(近似値)化】\n厳密式を小数の近似値に変換します(evalfと同等)。\n例: approx(pi) → 3.14159265" },
  { "evalf", "【近似値の評価】\n式を浮動小数点で評価します。\n例: evalf(sqrt(2))" },
  { "eval", "【式の評価】\n代入済み変数を使って式を再評価します。" },
  { "floor", "【床関数 (切り捨て)】\nx 以下の最大の整数を返します。\n例: floor(-1.2) → -2" },
  { "ceil", "【天井関数 (切り上げ)】\nx 以上の最小の整数を返します。\n例: ceil(1.2) → 2" },
  { "round", "【四捨五入】\n整数または小数第 n 位で丸めます。\n例: round(3.14159, 2) → 3.14" },
  { "sign", "【符号関数】\n負なら -1、0 なら 0、正なら 1 を返します。" },
  { "max", "【最大値】\n引数またはリスト内の最大値を返します。" },
  { "min", "【最小値】\n引数またはリスト内の最小値を返します。" },
  { "sin", "【正弦 (sin)】\n角度設定に応じ sin(x) を記号/数値計算。" },
  { "cos", "【余弦 (cos)】\n角度設定に応じ cos(x) を記号/数値計算。" },
  { "tan", "【正接 (tan)】\n角度設定に応じ tan(x) を記号/数値計算。" },
  { "asin", "【逆正弦 (arcsin)】\n-pi/2..pi/2 の範囲で逆正弦を返します。" },
  { "acos", "【逆余弦 (arccos)】\n0..pi の範囲で逆余弦を返します。" },
  { "atan", "【逆正接 (arctan)】\n-pi/2..pi/2 の範囲で逆正接を返します。" },
  { "sinh", "【双曲線正弦】\n(exp(x)-exp(-x))/2 を計算します。" },
  { "cosh", "【双曲線余弦】\n(exp(x)+exp(-x))/2 を計算します。" },
  { "tanh", "【双曲線正接】\nsinh(x)/cosh(x) を計算します。" },
  { "exp", "【指数関数 e^x】\n自然対数の底 e のべき乗を計算します。" },
  { "ln", "【自然対数 log_e】\ne を底とする対数 ln(x) を計算します。" },
  { "log10", "【常用対数 log_10】\n10 を底とする対数を計算します。" },
  { "sqrt", "【平方根 √】\n式の平方根を記号的に整理・計算します。\n例: sqrt(12) → 2*sqrt(3)" },
  { "plot", "【2D 関数グラフ描画】\n式 y=f(x) のグラフを描画します。\n例: plot(sin(x), x=-5..5)\n描画中[Esc]でシェルに戻れます。" },
  { "plotfunc", "【関数グラフ描画】\n陰関数・陽関数グラフを描画します。\n例: plotfunc(x^2-1, x)" },
  { "plotparam", "【2D 媒介変数曲線】\n[x(t), y(t)] 形式の曲線を描画します。\n例: plotparam([cos(t),sin(t)], t=0..2*pi)" },
  { "plotpolar", "【極座標グラフ r=f(θ)】\n極方程式 r=f(theta) を描画します。\n例: plotpolar(1+cos(t), t=0..2*pi)" },
  { "plotseq", "【数列・漸化式ウェブ図】\nu_{n+1}=f(u_n) の収束の様子のクモの巣図を描画します。" },
  { "plotlist", "【折れ線グラフ】\nリストの値を折れ線グラフとして描画します。" },
  { "plotfield", "【勾配場 (方向場)】\n微分方程式 y'=f(x,y) の方向場を描画します。" },
  { "plot3d", "【3D 曲面グラフ】\nz=f(x,y) や陰曲面の3D立体描画を行います。\n例: plot3d(sin(x)*cos(y), x=-3..3, y=-3..3)" },
  { "histogram", "【ヒストグラム描画】\nデータの度数分布ヒストグラムを描画します。" },
  { "barplot", "【棒グラフ描画】\nデータリストから棒グラフを描画します。" },
  { "linear_regression_plot", "【線形回帰 + 散布図】\n最小二乗法による直線 y=a*x+b と点を描画します。" },
  { "mean", "【算術平均】\nデータリストの平均値を計算します。" },
  { "stddev", "【標準偏差】\nデータリストの標準偏差を計算します。" },
  { "variance", "【分散】\nデータリストの分散を計算します。" },
  { "median", "【中央値】\nデータの中央値を計算します。" },
  { "normald", "【正規分布の確率密度】\n平均 m, 標準偏差 s の正規分布値を計算します。" },
  { "binomial", "【二項分布確率】\n試行回数 n, 確率 p で k 回成功する確率。" },
  { "rand", "【一様乱数】\n0..n-1 の整数乱数、または [0,1) 実数乱数を返します。" },
  { "makelist", "【リスト生成】\n式・変数・範囲・刻みからリストを作ります。\n例: makelist(k^2, k, 1, 5)" },
  { "seq", "【数列/リスト生成】\n式を反復評価してリストを生成します。\n例: seq(2*k+1, k, 0, 4)" },
  { "range", "【整数列リスト】\n0 から n-1 (または a..b) の整数リストを作ります。\n例: range(1, 6)" },
  { "len", "【長さ・要素数】\nリスト・文字列などの要素数を返します。" },
  { "append", "【リストに要素を追加】\nリスト末尾に要素を追加した新リストを返します。" },
  { "sort", "【リストのソート】\n数値や文字列のリストを昇順に並べ替えます。" },
  { "apply", "【関数を各要素へ適用】\nリストの全要素に関数を適用します。\n例: apply(x->x^2, [1,2,3])" },
  { "purge", "【変数の削除】\n代入した変数の定義を消去し記号に戻します。\n例: purge(a)" },
  { "assume", "【変数に仮定を設定】\n実数・正など条件を設定します。\n例: assume(x>0)" },
  { "about", "【変数の型・仮定を確認】\nassume で設定された仮定等を表示します。" },
  { "restart", "【CAS セッション初期化】\n全変数・仮定・履歴状態をリセットします。" },
  { "point", "【2D/3D 点オブジェクト】\n座標を指定して点を作成・描画します。\n例: point(1, 2)" },
  { "line", "【直線オブジェクト】\n2点や方程式から直線を作成・描画します。" },
  { "segment", "【線分オブジェクト】\n2点を結ぶ線分を作成・描画します。" },
  { "circle", "【円オブジェクト】\n中心と半径(または点)から円を描画します。" },
  { "polygon", "【多角形オブジェクト】\n頂点リストから多角形を描画します。" }
};

static void appendWrappedAscii(char * dst, size_t dstCap, const char * text) {
  if (!text || !text[0]) return;
  size_t curLen = strlen(dst);
  const int kMaxCharsPerLine = 21;
  const char * p = text;
  while (*p && curLen + 4 < dstCap) {
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) break;
    if (*p == '\n') {
      dst[curLen++] = '\n';
      dst[curLen] = '\0';
      ++p;
      continue;
    }
    int lineChars = 0;
    const char * lastSpace = nullptr;
    const char * q = p;
    while (*q && *q != '\n' && lineChars < kMaxCharsPerLine) {
      if (*q == ' ') lastSpace = q;
      ++q;
      ++lineChars;
    }
    const char * endPos = q;
    if (*q && *q != '\n' && lastSpace && lastSpace > p) {
      endPos = lastSpace;
    }
    size_t copyLen = static_cast<size_t>(endPos - p);
    if (curLen + copyLen + 2 >= dstCap) {
      copyLen = dstCap - curLen - 2;
    }
    if (copyLen == 0) break;
    memcpy(dst + curLen, p, copyLen);
    curLen += copyLen;
    dst[curLen++] = '\n';
    dst[curLen] = '\0';
    p = endPos;
  }
}

static void normalizeCmdName(const char * src, char * out, size_t outCap) {
  if (!src || outCap == 0) {
    if (outCap > 0) out[0] = '\0';
    return;
  }
  while (*src == ' ') ++src;
  size_t i = 0;
  while (*src && *src != '(' && *src != ' ' && i + 1 < outCap) {
    out[i++] = *src++;
  }
  out[i] = '\0';
}

} // namespace

extern "C" void khycas_show_category_help(int categoryId, const char * categoryLabel) {
  if (categoryId >= 0 && categoryId < 24) {
    BrainNotifyCustomHelp(kCategoryHelp[categoryId].title, kCategoryHelp[categoryId].desc);
    return;
  }
  const char * title = (categoryLabel && categoryLabel[0]) ? categoryLabel : "Commands list";
  BrainNotifyCustomHelp(title,
    "【コマンド一覧】\n"
    "[F3/Enter/→]: 選択項目を開く\n"
    "[PgUp/PgDn]: ページ送り\n"
    "[Esc/←]: シェルへ戻る");
}

extern "C" void khycas_show_command_help(
    const char * cmdName,
    const char * enDesc,
    const char * example1,
    const char * example2,
    const char * categoryLabel) {
  if (!cmdName || !cmdName[0]) {
    khycas_show_category_help(0, categoryLabel);
    return;
  }

  char norm[64];
  normalizeCmdName(cmdName, norm, sizeof(norm));

  const size_t kTableLen = sizeof(kCommandHelp) / sizeof(kCommandHelp[0]);
  for (size_t i = 0; i < kTableLen; ++i) {
    if (strcmp(norm, kCommandHelp[i].name) == 0) {
      char body[768];
      snprintf(body, sizeof(body), "%s", kCommandHelp[i].desc);
      if (example1 && example1[0] && strstr(body, "例:") == nullptr) {
        size_t len = strlen(body);
        if (len + 32 < sizeof(body)) {
          snprintf(body + len, sizeof(body) - len, "\n\n例1: %s%s%s",
                   (example1[0] == '#') ? "" : norm,
                   (example1[0] == '#') ? (example1 + 1) : "(",
                   (example1[0] == '#') ? "" : "");
          if (example1[0] != '#') {
            strncat(body, example1, sizeof(body) - strlen(body) - 4);
            strncat(body, ")", sizeof(body) - strlen(body) - 1);
          }
        }
      }
      BrainNotifyCustomHelp(cmdName, body);
      return;
    }
  }

  // Fallback for commands outside the top curated Japanese dictionary
  char title[64];
  snprintf(title, sizeof(title), "%s", cmdName);

  char body[768];
  body[0] = '\0';
  if (categoryLabel && categoryLabel[0]) {
    snprintf(body, sizeof(body), "【カテゴリ: %s】\n", categoryLabel);
  } else {
    snprintf(body, sizeof(body), "【CASコマンド解説】\n");
  }
  if (enDesc && enDesc[0]) {
    appendWrappedAscii(body, sizeof(body), enDesc);
  } else {
    strncat(body, "Xcas / Giac の組み込み関数です。\n", sizeof(body) - strlen(body) - 1);
  }

  if (example1 && example1[0]) {
    strncat(body, "\n例1:\n", sizeof(body) - strlen(body) - 1);
    char exBuf[128];
    if (example1[0] == '#') {
      snprintf(exBuf, sizeof(exBuf), "%s", example1 + 1);
    } else {
      snprintf(exBuf, sizeof(exBuf), "%s(%s)", norm, example1);
    }
    appendWrappedAscii(body, sizeof(body), exBuf);
  }
  if (example2 && example2[0]) {
    strncat(body, "例2:\n", sizeof(body) - strlen(body) - 1);
    char exBuf2[128];
    if (example2[0] == '#') {
      snprintf(exBuf2, sizeof(exBuf2), "%s", example2 + 1);
    } else {
      snprintf(exBuf2, sizeof(exBuf2), "%s(%s)", norm, example2);
    }
    appendWrappedAscii(body, sizeof(body), exBuf2);
  }

  strncat(body, "\n[F3]挿入 [Ent]例1 [F1]詳細 [←/Esc]戻る",
          sizeof(body) - strlen(body) - 1);
  BrainNotifyCustomHelp(title, body);
}

extern "C" void khycas_show_var_help(const char * itemText) {
  if (!itemText) itemText = "";
  if (strncmp(itemText, "purge(", 6) == 0) {
    BrainNotifyCustomHelp("purge (変数消去)",
      "【変数の定義を消去】\npurge(変数名) を挿入して定義済み変数を解放します。\n\n"
      "・変数の上で [BackSpace] を押すと直接削除できます。");
  } else if (strncmp(itemText, "assume(", 7) == 0) {
    BrainNotifyCustomHelp("assume (変数の仮定)",
      "【変数に条件を設定】\nassume(x>0) 等で変数の範囲・実数条件を指定します。\n\n"
      "・積分や簡約の結果が精密になります。");
  } else if (strncmp(itemText, "restart", 7) == 0) {
    BrainNotifyCustomHelp("restart (セッション初期化)",
      "【変数をすべてリセット】\n定義済みの変数・仮定をすべて初期化します。");
  } else {
    char title[64];
    normalizeCmdName(itemText, title, sizeof(title));
    if (!title[0]) snprintf(title, sizeof(title), "変数 (Vars)");
    char body[512];
    snprintf(body, sizeof(body),
      "【定義済み変数】\n%s\n\n"
      "・[F3 / Enter]: 変数名を挿入\n"
      "・[BackSpace]: この変数を削除 (purge)\n"
      "・[Esc]: シェルへ戻る",
      itemText);
    BrainNotifyCustomHelp(title, body);
  }
}

extern "C" void khycas_show_menu_help(const char * itemText) {
  if (!itemText) itemText = "";
  const char * body =
    "【KhyCAS メニュー】\n"
    "[F3 / Enter]: 実行\n"
    "[Esc]: シェルへ戻る\n\n"
    "セッションの保存・読込・履歴消去・設定変更ができます。";
  if (strstr(itemText, "Save")) {
    body = "【セッション保存】\n現在の入力履歴と変数定義をファイルへ保存します。";
  } else if (strstr(itemText, "Load") || strstr(itemText, "Open")) {
    body = "【読込・開く】\n保存済みのセッション(.xw)やスクリプト(.py)を読み込みます。";
  } else if (strstr(itemText, "New")) {
    body = "【新規セッション】\n現在の変数・履歴をクリアし新しい作業領域を作ります。";
  } else if (strstr(itemText, "Run")) {
    body = "【実行】\nセッション内の式やスクリプトを上から順に再評価します。";
  } else if (strstr(itemText, "Script")) {
    body = "【スクリプト編集】\nPython/Xcasスクリプトを編集・消去します。";
  } else if (strstr(itemText, "Clear history")) {
    body = "【履歴消去】\nコンソールの計算履歴をすべて消去します。";
  } else if (strstr(itemText, "Configuration")) {
    body = "【環境設定】\n角度単位(弧度法/度数法)、複素数モード、言語などを設定します。";
  } else if (strstr(itemText, "Shortcuts")) {
    body = "【ショートカット説明】\nKhyCAS組み込みのキーボードショートカットを表示します。";
  } else if (strstr(itemText, "Matrix")) {
    body = "【行列エディタ】\n表形式で数値を入力し行列を簡単作成します。";
  } else if (strstr(itemText, "Quit")) {
    body = "【KhyCASを終了】\nLiliumホーム画面へ戻ります([HOME]/[F10]でも可)。";
  }
  BrainNotifyCustomHelp("KhyCAS メニュー", body);
}

#endif // KHYCAS_HELP_JA_H
