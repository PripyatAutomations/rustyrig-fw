# Build with makepkg -f
pkgname=('rrclient' 'rrserver')
pkgver=20250702
pkgrel=1
pkgdesc="rustyrig-fw client and server"
arch=('x86_64')
url="https://example.com/"
license=('MIT')
source=("rr-${pkgver}.tar.gz")
md5sums=('SKIP') # or actual sum

build() {
   cd "$srcdir/rr-$pkgver"
   make
}

package_rrclient() {
   cd "$srcdir/rr-$pkgver"
   install -Dm755 build/client/rrclient "$pkgdir/usr/bin/rrclient"
}

package_rrserver() {
   cd "$srcdir/rr-$pkgver"
   install -Dm755 build/radio/rrserver "$pkgdir/usr/bin/rrserver"
}
