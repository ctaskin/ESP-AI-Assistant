# Ek kök sertifikalar

Buradaki `.pem` / `.der` dosyaları ESP-IDF'in yerleşik sertifika paketinin **üzerine**
eklenir (`CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE`, yol `certs`). Yerleşik paket
değiştirilmez, yalnızca genişletilir. Başka uzantılı dosyalar (bu README gibi)
`gen_crt_bundle.py` tarafından yok sayılır.

## GlobalSign_Root_CA.pem — neden burada

`api.openai.com` bağlantısı `No matching trusted root certificate found` ile düşüyordu.
Sunucunun gönderdiği zincir:

```
api.openai.com  ← WE1  ← GTS Root R4  ← GlobalSign Root CA
```

Son halka, **GlobalSign Root CA tarafından çapraz imzalanmış** GTS Root R4'tür. mbedTLS
zincirin tepesindeki sertifikanın **verenini** arar; ESP-IDF 6.1 paketinde GTS Root R1–R4
bulunuyor ama klasik `GlobalSign Root CA` (1998, `OU=Root CA`) **bulunmuyor** — Mozilla
deposundan çıkarılmış. Çapraz imza desteği (`MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY`)
de aramayı verene göre yaptığı için bu halkayı kurtaramıyor.

Bu kök eklenince zincirin tepesi doğrulanabiliyor.

| | |
|---|---|
| Subject / Issuer | `C=BE, O=GlobalSign nv-sa, OU=Root CA, CN=GlobalSign Root CA` |
| Geçerlilik | 1998-09-01 → **2028-01-28** |
| SHA-256 | `EB:D4:10:40:E4:BB:3E:C7:42:C9:E3:81:D3:1E:F2:A4:1A:48:B6:68:5C:96:E7:CE:F3:C1:DF:6C:D4:33:1C:99` |

Parmak izini GlobalSign'ın yayımladığı değerle karşılaştırıp doğrulayabilirsin.

Kökün kendi imzası SHA-1'dir; bu bir sorun değil, çünkü paket sertifikaları sıkıştırılmış
saklar: yalnızca konu adı ve açık anahtar gömülür, kökün kendi imzası hiç doğrulanmaz.

**2028 Ocak'ta bu kök sona eriyor.** OpenAI o tarihten önce zincirini büyük ihtimalle
değiştirir; değiştirmezse bağlantı yeniden `TLS: sertifika dogrulanamadi` verir ve zinciri
baştan çıkarmak gerekir.

## Yeni bir kök eklemek

PEM dosyasını buraya kopyalamak yeterli; seçenek `sdkconfig.defaults` içinde zaten açık.
Kök sertifikalar geneldir, gizli veri değildir — depoya işlenebilir.
