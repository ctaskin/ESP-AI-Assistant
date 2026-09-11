# Ceko v0.1 doğrulama

## Yapılanlar

- ESP-IDF 5.5.2 / ESP32-S3 için tüm uygulama C dosyaları derlendi.
- Donanımdan bağımsız C testleri AddressSanitizer + UndefinedBehaviorSanitizer altında geçti.
  Bu çalışma ortamında ptrace nedeniyle LeakSanitizer kapatıldı; sızıntı testi yapılmış sayılmaz.
- Testler: sessizlikte kayıt göndermeme, konuşma bitişi, uzun kayıt reddi, WebSocket mesaj
  parçalama/birleştirme, devam frame'leri arasında ping, sıra/boyut hatası reddi,
  resampler durumunun paket sınırlarında korunması, 16↔24 kHz örnek sayısı,
  Nyquist üstü frekans bastırma, RMS ve çıkış tampon kapasitesi kontrolü.
- Tarayıcı önizlemesinin JavaScript sözdizimi `node --check` ile doğrulandı.
  Bu ortamda Chromium olmadığı için görsel ekran görüntüsü testi yapılmadı.

## Tam firmware derlemesi — başarılı

ESP-IDF 5.5.2 ve kilit dosyasındaki bağımlılıklarla `idf.py build` tamamlandı.
Uygulama, bootloader, bölüm tablosu ve konuşma modeli üretildi.

| Bileşen | Dosya boyutu | Ayrılan bölüm |
|---|---:|---:|
| Uygulama | 3.308.608 bayt | 4.194.304 bayt; %21 boş |
| MultiNet konuşma modeli | 2.761.093 bayt | 4.128.768 bayt |

ELF dosyasında `speech_start`, `realtime_start`, `face_start` ve `board_audio_init`
sembolleri doğrulandı. Tam kod yolunun bağlanması için yalnızca derleme ortamında
gerçek olmayan, boş olmayan Wi-Fi/API değerleri kullanıldı. Cihaz çalıştırılmadı,
API çağrısı yapılmadı. Bu yerel ayarlar ve binary dosyaları kaynak ZIP'ine dahil değil.
Kendi ayarlarını girip derlediğinde firmware boyutu biraz değişebilir.

## Yapılmayanlar

- Karta flash, fiziksel ekran/mikrofon/hoparlör testi.
- Türkçe “hey ceko” algılama başarısı ve saat başına yanlış aktivasyon ölçümü.
- Gerçek OpenAI API isteği ve API hesabında model erişimi kontrolü.
- Gerçek Türkçe ses kalitesi, yanıt gecikmesi, uzun süreli bellek/bağlantı dayanıklılığı.

## İlk fiziksel test sırası

1. Offline face demo ile ekran yönü, renk, göz kırpma ve ağız hareketini kontrol et.
2. Demo modunu kapat. Wi-Fi ve gerçek API anahtarını kendi bilgisayarında gir; tam flash yap.
3. Seri monitörde `Ready: model=...` ve serbest PSRAM logunu kontrol et.
4. Yakından normal sesle “hey ceko” de; yeşil gözlere geçişi doğrula.
5. Güncel veri gerektirmeyen bir soruyla test et; örneğin “Bana kısa bir bilmece sor.”
6. Hoparlör çalışırken yeniden uyanmadığını; cevap bitince yeniden çağrılabildiğini kontrol et.
7. Sessiz uyandırma ve 20 saniyeyi aşan konuşmanın API'ye gönderilmediğini seri logdan doğrula.
8. Wi-Fi kesildiğinde hata sonrası beklemeye döndüğünü ve ağız animasyonunun durduğunu kontrol et.

Wake algılanmazsa önce slot/pin/kazanç ile gerçek mikrofon verisini doğrula. Sonra
`CEKO_WAKE_PHRASE` telaffuz yazımını ve eşiği ayarla. Hazır Türkçe WakeNet varmış gibi kabul etme.
