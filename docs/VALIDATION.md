# Ceko doğrulama

## v0.2 (kalıcı oturum, hafıza, web araması) — bu ortamda yapılanlar

- Donanımdan bağımsız testlere iki modül eklendi ve ASan/UBSan altında geçti:
  `session_policy` (bağlan/yenile/geri çekilme, bağlantı koparsa davranış, sonsuz
  oturum durumu) ve `history` (halka tampon, UTF-8 sınırında kesme, kontrol
  karakterlerinin temizlenmesi, kapasiteye sığdırma).
- `tools/syntax-check.sh`: `main/realtime.c`, `main/rt_openai.c` ve `main/rt_gemini.c`
  dosyaları `tests/stubs` altındaki taklit başlıklarla `-Wall -Wextra -Werror`
  tip kontrolünden geçti; Gemini dalı ayrıca ayrı derlenerek kontrol edildi.

## v0.2 — bu ortamda yapılamayanlar

- **ESP-IDF derlemesi yapılmadı.** Bu makinede ESP-IDF kurulu değil; `idf.py build`
  çalıştırılmadı. Taklit başlıklarla tip kontrolü gerçek derlemenin yerine geçmez;
  özellikle `esp_websocket_client_config_t` alan adları ve cJSON imzaları gerçek
  bileşenlere karşı doğrulanmalı.
- **Canlı API çağrısı yapılmadı.** OpenAI'de `reasoning.effort`, giriş transkripsiyonu
  ve `tools:[{type:"web_search"}]`; Gemini'de tüm mesaj şeması (`setup`,
  `realtimeInput.audio`, `activityStart/End`, `serverContent`, `sessionResumptionUpdate`)
  dokümantasyondan yazıldı, doğrulanmadı.
- Gecikme kazancı ölçülmedi; kalıcı oturumun uzun süreli bellek/bağlantı dayanıklılığı
  ve yeniden bağlanma davranışı fiziksel kartta denenmedi.

## v0.1 sırasında yapılanlar

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

## v0.2 için eklenecek fiziksel testler

1. Açılışta seri logda `session open (openai|gemini)` satırını gör; ilk soruya kadar
   bağlantının kurulmuş olduğunu doğrula.
2. Peş peşe iki soru sor; ikincisinde birinciye atıf yap ("az önce ne dedim?").
3. Wi-Fi'yi kapat/aç; arka planda yeniden bağlandığını ve sonraki sorunun çalıştığını gör.
4. Oturum yenileme mesajını (`renewing session`) bekle; sonrasında hafızanın
   korunduğunu doğrula.
5. Güncel bilgi gerektiren bir soru sor ("bugün ... fiyatı ne?") ve aramanın
   çalışıp çalışmadığını kaydet.
6. İlk yanıt gecikmesini v0.1 ile karşılaştırarak ölç.

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
