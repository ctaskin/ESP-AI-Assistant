# Ek kök / ara sertifikalar

Bu klasör normalde **boştur**. ESP-IDF'in yerleşik sertifika paketi (145 kök, Mozilla NSS
deposundan) çoğu genel API için yeterlidir; `api.openai.com`'un dayandığı Google Trust
Services kökleri (GTS Root R1–R4) de paketin içindedir.

Buraya yalnızca paketin **çözemediği** bir sertifika eklenir. İki tipik durum:

1. **Sunucu ara sertifikayı göndermiyor.** Zincirin tepesi yaprak sertifika olarak kalır ve
   mbedTLS onun verenini (örneğin `Google Trust Services / WE1`) pakette bulamaz. Tarayıcılar
   eksik arayı AIA ile kendileri indirir; mbedTLS indirmez. Çözüm, ara sertifikayı buraya
   koymaktır.
2. **Ağ TLS'i araya giriyor.** Kurum ağı veya router kendi kökünü kullanıyorsa o kökü buraya
   koymak gerekir. Bu, trafiğin o kutuda açıldığını kabul etmek demektir; tercih edilmez.

## Kullanımı

PEM dosyalarını (`.pem`, `-----BEGIN CERTIFICATE-----` bloğu) bu klasöre kopyala, sonra:

```
idf.py menuconfig
  Component config
    ESP-TLS / mbedTLS
      Certificate Bundle
        [*] Add custom certificates to the default bundle
            Custom certificate bundle path: certs
```

`idf.py build` sırasında bu klasördeki sertifikalar yerleşik paketin üzerine eklenir.
Paket sertifikaları sıkıştırılmış saklar: yalnızca konu adı ve açık anahtar gömülür.

Buraya konan dosyalar depoya işlenebilir — kök sertifikalar geneldir, gizli veri değildir.
