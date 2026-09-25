#include <iostream>
#include <algorithm>
#include <set>
#include <unordered_map>
using namespace std;
using ll = long long;
const int N = 2e5 + 5;
const ll INF = 1e18;
int n, k0;
int a[N], k[N];

set<int> s_a;
unordered_map<int, int> mp, rv;

struct Line {
  ll m, c;
  Line(ll m = 0, ll c = -INF) : m(m), c(c) {}
  ll calc(ll x) { return m * x + c; }
};
ll calc(Line l, int id) { return l.calc(rv[id]); }

#define lp (p << 1)
#define rp (p << 1 | 1)

Line tr[N << 2];
void insert(int p, int l, int r, Line line) {
  Line &cur = tr[p];
  if (cur.c == -INF) {
    cur = line;
    return;
  }
  int mid = (l + r) >> 1;
  if (calc(line, mid) > calc(cur, mid))
    swap(cur, line);
  if (l == r)
    return;
  if (calc(line, l) > calc(cur, l))
    insert(lp, l, mid, line);
  else if (calc(line, r) > calc(cur, r))
    insert(rp, mid + 1, r, line);
}
ll query(int p, int l, int r, int id) {
  ll y = calc(tr[p], id);
  if (l == r)
    return y;
  int mid = (l + r) >> 1;
  if (id <= mid)
    y = max(y, query(lp, l, mid, id));
  else
    y = max(y, query(rp, mid + 1, r, id));
  return y;
}

#undef lp
#undef rp

ll dp[N], ans = 0;

int main() {
  ios::sync_with_stdio(0);
  cin.tie(0), cout.tie(0);
  cin >> n >> k0;
  for (int i = 1; i <= n; i++)
    cin >> a[i];
  for (int i = 1; i <= n; i++)
    cin >> k[i];
  for (int i = 1; i <= n; i++)
    s_a.insert(a[i]);
  int m = 0;
  for (int x : s_a)
    mp[x] = ++m, rv[m] = x;
  for (int i = 1; i <= n; i++) {
    if (i == 2)
      insert(1, 1, m, Line(k0, 0));
    else if (i > 2)
      insert(1, 1, m, Line(k[i - 2], dp[i - 2]));
    if (i == 1)
      dp[i] = a[i];
    else
      dp[i] = max(dp[i - 1] + a[i], query(1, 1, m, mp[a[i]]));
    ans = max(ans, dp[i]);
  }
  cout << ans;

  return 0;
}
