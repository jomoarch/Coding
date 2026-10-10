#include <iostream>
#include <string>
#include <cstring>
using namespace std;
using ull = unsigned long long;
const int N = 1048578; // 2^20+2
int t;
int n;
string s;

ull pw[N];
ull h[N];
ull get_hash(int l, int r) { return h[r] - pw[r - l + 1] * h[l - 1]; }

int cntl[N], cntr[N];
bool fl[26];
int cand[27];

int main() {
  cin >> t;
  pw[0] = 1u;
  for (int i = 1; i <= N - 2; i++)
    pw[i] = pw[i - 1] * 131u;
  while (t--) {
    cin >> s;
    n = s.size();
    if (n <= 2) {
      cout << "0\n";
      continue;
    }
    s = '*' + s;
    h[0] = 0;
    for (int i = 1; i <= n; i++)
      h[i] = h[i - 1] * 131u + s[i];
    cntl[0] = 0;
    memset(fl, 0, sizeof(fl));
    for (int i = 1; i <= n; i++) {
      int now = s[i] - 'a';
      fl[now] ^= 1;
      cntl[i] = cntl[i - 1];
      if (fl[now])
        cntl[i]++;
      else
        cntl[i]--;
    }
    cntr[n + 1] = 0;
    memset(fl, 0, sizeof(fl));
    for (int i = n; i; i--) {
      int now = s[i] - 'a';
      fl[now] ^= 1;
      cntr[i] = cntr[i + 1];
      if (fl[now])
        cntr[i]++;
      else
        cntr[i]--;
    }

    ull ans = 0;
    memset(cand, 0, sizeof(cand));
    for (int i = 1; i <= n - 2; i++) {
      for (int j = cntl[i]; j <= 26; j++) {
        cand[j]++;
      }
      ull ha = get_hash(1, i + 1);
      for (int j = i + 2; j <= n; j += i + 1) {
        if (get_hash(j - i - 1, j - 1) != ha)
          break;
        ans += cand[cntr[j]];
      }
    }
    cout << ans << '\n';
  }
  return 0;
}