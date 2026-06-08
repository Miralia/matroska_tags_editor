param(
  [string]$DownloadsDir = $env:VCPKG_DOWNLOADS
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $DownloadsDir) {
  throw "DownloadsDir is required."
}

$archives = @(
  @{
    Name = "Matroska-Org-libebml-release-1.4.5.tar.gz"
    Url = "https://codeload.github.com/Matroska-Org/libebml/tar.gz/refs/tags/release-1.4.5"
    Sha512 = "284da9b7a1415585bbcfffc87101c63f1dd242bb09d88a731597127732a2f8064fd35e0a718fdcde464714b71e3f7dcc8285f291889629aba6997c38e0575dfb"
  },
  @{
    Name = "Matroska-Org-libmatroska-release-1.7.1.tar.gz"
    Url = "https://codeload.github.com/Matroska-Org/libmatroska/tar.gz/refs/tags/release-1.7.1"
    Sha512 = "abb4fb4b527266944b1a59516866462498675c5e71bb679758894dff6156169d7132dddaa2e2ef6187a6dbce4a4aa377eeb75dd869268fd44933c769b34be5b9"
  },
  @{
    Name = "libexpat-libexpat-R_2_8_1.tar.gz"
    Url = "https://codeload.github.com/libexpat/libexpat/tar.gz/refs/tags/R_2_8_1"
    Sha512 = "eb1db97184b5812d7c15afcd2219478295316c21d8a0566e002cc6f5e4ca576a8ed403a290e5b84f7b152543f38f81a26bb69fb4e5b0809d0d0ad310e47d4bbc"
  },
  @{
    Name = "libjpeg-turbo-libjpeg-turbo-3.1.4.1.tar.gz"
    Url = "https://codeload.github.com/libjpeg-turbo/libjpeg-turbo/tar.gz/refs/tags/3.1.4.1"
    Sha512 = "5c67fa6a528e35963736e0361ae6952f6f253b74cd31e8795de60a80287b123c4f35a333ca03934011b01f72110c13b26d13320fce2969287428cebde153c730"
  },
  @{
    Name = "pnggroup-libpng-v1.6.58.tar.gz"
    Url = "https://codeload.github.com/pnggroup/libpng/tar.gz/refs/tags/v1.6.58"
    Sha512 = "65f54d805e1f7c46a5fc335b984e4cbd4f934e0f02fbf6673c13800b49a4c11fbeb4098eebfb33079527a56c3d933e97631f91ab68dbb31442982784f9241ace"
  },
  @{
    Name = "madler-zlib-v1.3.2.tar.gz"
    Url = "https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.2"
    Sha512 = "16fea4df307a68cf0035858abe2fd550250618a97590e202037acd18a666f57afc10f8836cbbd472d54a0e76539d0e558cb26f059d53de52ff90634bbf4f47d4"
  },
  @{
    Name = "webmproject-libwebp-v1.6.0.tar.gz"
    Url = "https://codeload.github.com/webmproject/libwebp/tar.gz/refs/tags/v1.6.0"
    Sha512 = "298e0ad4c09392213baf5abb69d330c6203b618800073fe2df91d01d35034197c5d3e29a74573b06971473c52c74514f0e6e0f6c8162f923e2dd15cb1a692aef"
  },
  @{
    Name = "memononen-nanosvg-93ce879dc4c04a3ef1758428ec80083c38610b1f.tar.gz"
    Url = "https://codeload.github.com/memononen/nanosvg/tar.gz/93ce879dc4c04a3ef1758428ec80083c38610b1f"
    Sha512 = "14ecaf11efd2f0b983847ded557557a2919cc04fc5e9748118cc0bd33fccae2688afc0dc182ebb8c0deb4b599c697f140185644a087c702fba1e6368f5a5b89c"
  },
  @{
    Name = "KhronosGroup-OpenGL-Registry-0b449b97cdf1043eef5e1f0e235cbbab6ec10c86.tar.gz"
    Url = "https://codeload.github.com/KhronosGroup/OpenGL-Registry/tar.gz/0b449b97cdf1043eef5e1f0e235cbbab6ec10c86"
    Sha512 = "148e1bfe4cc199bcc2c23b22d0b3e4988a29389d7f510ba4a6340672dbb7ab99bb836d2c08587499484df704d51a1adf4f0dc3a30d5ad8977ee0ad339163b17e"
  },
  @{
    Name = "KhronosGroup-EGL-Registry-3ae2b7c48690d2ce13cc6db3db02dfc0572be65e.tar.gz"
    Url = "https://codeload.github.com/KhronosGroup/EGL-Registry/tar.gz/3ae2b7c48690d2ce13cc6db3db02dfc0572be65e"
    Sha512 = "c7b09ded4964fa427546bd345a29325105b79079b59642214dc8f04de113f42de2bc4272dbbbd4a801d92afc20297442fdfa12043a0900cf1e2b1cd83f260883"
  },
  @{
    Name = "PCRE2Project-pcre2-pcre2-10.47.tar.gz"
    Url = "https://codeload.github.com/PCRE2Project/pcre2/tar.gz/refs/tags/pcre2-10.47"
    Sha512 = "4deef8ce95711e65fe07624e6b2aace794594adb15e8363a0279a7b947bf5c75a5858fbdc5251d0a28a7ca97ae8bba561aa5f85805d5c07d417d3e7b3b3486a4"
  },
  @{
    Name = "zherczeg-sljit-45f910b78c6605ebf5b53d3ec7cb00f2312fe417.tar.gz"
    Url = "https://codeload.github.com/zherczeg/sljit/tar.gz/45f910b78c6605ebf5b53d3ec7cb00f2312fe417"
    Sha512 = "c05c83cc762f430c01e2aaf876aaac41a70b67ed8b91bc81102ad527c8921c5e75b41bab35bb8237dd5f53fecd7b8f31206865efffce2ea0a1aa9c87079fc643"
  },
  @{
    Name = "tukaani-project-xz-v5.8.3.tar.gz"
    Url = "https://codeload.github.com/tukaani-project/xz/tar.gz/refs/tags/v5.8.3"
    Sha512 = "8fb5e6a13397d259d8ff7484f9b63f8a6752ff1c63e1a4601170ad8175aadefb5126a1cae7f73370bfc6c2a0b4e1c0bad57a58fc5b781d3f7d45e5a483c091cc"
  },
  @{
    Name = "wxWidgets-wxWidgets-v3.3.1.tar.gz"
    Url = "https://codeload.github.com/wxWidgets/wxWidgets/tar.gz/refs/tags/v3.3.1"
    Sha512 = "8ad17582c4ba721ffe76ada4bb8bd7bc4b050491220aca335fd0506a51354fb789d5bc3d965f0f459dc81784d6427c88272e2acc2099cddf73730231b5a16f62"
  },
  @{
    Name = "wxWidgets-lexilla-27c20a6ae5eebf418debeac0166052ed6fb653bc.tar.gz"
    Url = "https://codeload.github.com/wxWidgets/lexilla/tar.gz/27c20a6ae5eebf418debeac0166052ed6fb653bc"
    Sha512 = "7e5de7f664509473b691af8261fca34c2687772faca7260eeba5f2984516e6f8edf88c27192e056c9dda996e2ad2c20f6d1dff1c4bd2f3c0d74852cb50ca424a"
  },
  @{
    Name = "wxWidgets-scintilla-0b90f31ced23241054e8088abb50babe9a44ae67.tar.gz"
    Url = "https://codeload.github.com/wxWidgets/scintilla/tar.gz/0b90f31ced23241054e8088abb50babe9a44ae67"
    Sha512 = "db1f3007f4bd8860fad0817b6cf87980a4b713777025128cf5caea8d6d17b6fafe23fd22ff6886d7d5a420f241d85b7502b85d7e52b4ddb0774edc4b0a0203e7"
  }
)

New-Item -ItemType Directory -Force -Path $DownloadsDir | Out-Null
foreach ($archive in $archives) {
  $destination = Join-Path $DownloadsDir $archive.Name
  if (Test-Path $destination) {
    Write-Host "Using cached $($archive.Name)"
    continue
  }

  $temporary = "$destination.tmp"
  Remove-Item -Force -ErrorAction SilentlyContinue $temporary

  for ($attempt = 1; $attempt -le 3; $attempt++) {
    try {
      Write-Host "Downloading $($archive.Name) (attempt $attempt)"
      Invoke-WebRequest -Uri $archive.Url -OutFile $temporary -UseBasicParsing
      break
    } catch {
      Remove-Item -Force -ErrorAction SilentlyContinue $temporary
      if ($attempt -eq 3) {
        throw
      }
      Start-Sleep -Seconds (5 * $attempt)
    }
  }

  $actual = (Get-FileHash -Path $temporary -Algorithm SHA512).Hash.ToLowerInvariant()
  if ($actual -ne $archive.Sha512) {
    Remove-Item -Force $temporary
    throw "SHA512 mismatch for $($archive.Name)"
  }

  Move-Item -Force $temporary $destination
}
