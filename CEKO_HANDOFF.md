# Ceko v0.1 — Geliştirme Handoff Belgesi

Tarih: 11 Eylül 2026  
Proje sahibi: Cahit Taşkın  
Durum: Firmware derlemesi tamamlanmış, fiziksel kart ve canlı API doğrulaması bekleyen prototip.

> **v0.2 notu:** Ağ katmanı yeniden yazıldı. Oturum artık kalıcı, sohbet aynı oturumda
> devam ediyor, servis (OpenAI Realtime / Gemini Live) `menuconfig`'den seçiliyor ve web
> araması eklendi. Gerekçe, fiyat karşılaştırması ve doğrulanmayanlar: `docs/PROVIDERS.md`.
> Aşağıdaki 3. ve 6. bölümler v0.2'ye göre güncellendi; v0.2 kodu ESP-IDF ile
> **derlenmedi**, canlı API ile **denenmedi**.

## 1. Amaç ve kullanıcının kararları

Waveshare ESP32-S3-Touch-AMOLED-1.32 kartıyla Türkçe konuşan masaüstü AI asistanı geliştiriliyor. Adı **Ceko**.

- ESP-IDF kullanılacak; Arduino/PlatformIO tabanına geçiş istenmedi.
- İlk servis OpenAI Realtime mini olacak.
- **Bas-konuş istenmiyor.** Kullanıcı “hey ceko” deyince cihaz dinlemeye geçmeli ve ardından söylediği soruyu kullanmalı.
- Ekranda göz kırpan iki göz; Ceko konuşurken hareket eden ağız olacak. Görünüm ileride değiştirilebilir.
- Şimdilik GitHub reposu açılmayacak; kullanıcı daha sonra açacak.
- Yerel model seçeneği görüşüldü ancak ertelendi. Mac donanım seçimi bu aşamanın işi değil.

Önceki konuşmada küçük backend önerilmiş olsa da teslim edilen v0.1 **doğrudan ESP32 → OpenAI bağlantısı** kullanıyor. Çalışması için açık bilgisayar veya bridge gerekmiyor. Backend henüz uygulanmadı.

## 2. Devralınacak dosyalar

Bu oturumdaki kaynak klasörü: `/workspace/scratch/508c151f36d0/ceko`  
Teslim edilmiş kaynak paketi: `/workspace/scratch/508c151f36d0/ceko-v0.1.zip`

Başka bilgisayara/oturuma geçerken ZIP ve bu belge birlikte aktarılmalı. Yukarıdaki çalışma alanı yollarının yeni ortamda bulunacağı varsayılmamalı. `ceko-incomplete.zip` eski, eksik taslaktır; başlangıç olarak kullanılmamalı.

| Proje içindeki yol | Sorumluluk |
|---|---|
| `README.md` | Kurulum, kullanım, sınırlar ve varsayılanlar |
| `docs/VALIDATION.md` | Önceki derleme/test kanıtları ve yapılmayan doğrulamalar |
| `docs/face-preview.html` | Bilgisayarda açılabilen yüz önizlemesi |
| `main/app_main.c`, `main/ceko.h` | Başlangıç, Wi-Fi/NTP, ortak durum ve arayüzler |
| `main/board.c` | Güç, ES8311, I2C/I2S mikrofon ve hoparlör |
| `main/face.c` | SH8601 AMOLED, LVGL 9, göz/ağız animasyonu |
| `main/speech.c` | AFE/VAD, WakeNet uyandırma, BOOT düğmesi ve kayıt |
| `main/touch.c` | Dokunmatik panel (codec I2C hattı, 0x15) |
| `main/capture_gate.c` | Konuşma bitişi, boş/uzun kayıt sınırları |
| `main/realtime.c` | Kalıcı TLS WebSocket oturumu, ses gönderme/çalma, hata yönetimi |
| `main/rt_proto.h`, `main/rt_openai.c`, `main/rt_gemini.c` | Servis protokolleri (OpenAI Realtime, Gemini Live) |
| `main/session_policy.c` | Bağlan/yenile/geri çekil kararları (host testli) |
| `main/history.c` | Yeniden bağlanmada taşınan kısa konuşma özeti (host testli) |
| `tests/stubs/`, `tools/syntax-check.sh` | ESP-IDF'siz makinede tip kontrolü |
| `main/audio_math.c` | 16↔24 kHz FIR örnekleme dönüşümü ve RMS |
| `main/ws_message.c` | Parçalı WebSocket mesajlarının sınırlı birleştirilmesi |
| `main/Kconfig.projbuild` | `menuconfig > Ceko` seçenekleri |
| `main/idf_component.yml`, `dependencies.lock` | Bağımlılık tanımı ve kilidi |
| `sdkconfig.defaults`, `partitions.csv` | Hedef donanım ve flash düzeni |
| `tests/test_core.c`, `tools/test.sh` | Donanımdan bağımsız C testleri |

## 3. Mevcut davranış ve mimari

1. Açılışta güç ve yüz başlatılır. Normal modda ayarlar kontrol edilip ses, Wi-Fi, NTP ve görevler başlatılır.
2. `IDLE`: Turkuaz gözler kırpılır. Mikrofon yerel AFE ve MultiNet tarafından işlenir; bekleme sesi API'ye gönderilmez.
3. Uyandırma algılanınca `LISTEN`: Gözler yeşile döner, sonraki ses PSRAM'de tutulur. Uyandırmayı tetikleyen çerçeve gönderilmez.
4. Konuşma sonrası 1 saniye sessizlik kaydı bitirir. `THINK` durumuna geçilir.
5. Kayıt, açılıştan beri açık tutulan WebSocket oturumundan gönderilir. OpenAI'de 24 kHz'e çevrilip `input_audio_buffer.commit` + `response.create`, Gemini'de 16 kHz olarak `activityStart`/`activityEnd` ile iletilir. Oturum kapalıysa o an açılır.
6. Gelen 24 kHz PCM ses 16 kHz'e çevrilip hoparlörde çalınır. `SPEAK` sırasında ağız, çalınan sesin RMS şiddetine göre hareket eder; fonem eşlemesi değildir.
7. Yanıt/çalma tamamlanınca oturum **açık kalır**. 600 ms akustik bekleme ardından yeniden `IDLE` olur.

Sohbet aynı oturumda sürer. Oturum ömrü dolmadan cihaz boştayken yenilenir; kopma veya yenileme olduğunda son turların kısa özeti (OpenAI) ya da `sessionResumption` handle'ı (Gemini) ile bağlam taşınır. Yanıt sırasında mikrofon verisi tüketilip atılır; söz kesme ve tam çift yönlü konuşma yoktur. AEC kapalıdır. Beklemedeki ses hâlâ gönderilmez: sunucu tarafı ses algılama kapalıdır.

Tek kayıt tamponu ağ görevine ödünç verilir; `IDLE` durumuna dönene kadar üzerine yazılmaması gerekir. Bu sahiplik kuralı, yeni sohbet veya söz kesme özellikleri eklenirken korunmalı.

## 4. Uyandırma: WakeNet9 “Hi ESP”

Sürekli dinleyen motor artık **WakeNet9 `wn9_hiesp`**. Önceki MultiNet7 denemesi bırakıldı:
MultiNet bir komut tanıyıcıdır, uyandırma sözcüğünden sonra çalışmak üzere tasarlanmıştır ve
bu yongada sürekli çalıştırıldığında gerçek zamanı yakalayamıyordu — çekirdek 1 doluyor,
`Ringbuffer of AFE(FEED) is full` uyarıları çıkıyor ve boştaki görev watchdog'u besleyemiyordu.
Fiziksel kartta hiçbir denemede tetiklenmedi.

Türkçe “ceko” için eğitilmiş hazır model bulunmuyor. Türkçe bir uyandırma sözcüğü istenirse
Espressif'e özel model eğittirmek gerekir; ayrı iş kalemidir. Sözcük
`menuconfig > ESP Speech Recognition > Load Multiple Wake Words`, eşik
`menuconfig > Ceko > WakeNet detection threshold` altındadır.

WakeNet yalnızca beklerken açıktır; Ceko konuşurken kapatılır, böylece kendi sesine uyanmaz.
Uyandırmanın yanında iki giriş yolu daha var: ekrana dokunma (`main/touch.c`) ve BOOT düğmesi.
Üçü de `ceko_state_try_listen()` üzerinden geçer; bu, kayıt tamponu ağ görevine ödünç
verilmişken (THINK/SPEAK) yeni kayıt açılmasını engelleyen tek kapıdır.

## 5. Donanım ve bağımlılıklar

Hedef kart: Waveshare ESP32-S3-Touch-AMOLED-1.32; 466×466 AMOLED, 8 MB flash, 8 MB PSRAM, ES8311. Başka boyuttaki Waveshare kartlarının pinleri kullanılmamalı.

Aşağıdakiler mevcut kaynak kodundaki pin eşlemeleridir; üretici referansına göre hazırlanmış, fiziksel kartta denenmemiştir.

| İşlev | GPIO |
|---|---|
| Güç etkinleştirme | 18 |
| Codec I2C SDA / SCL | 47 / 48 |
| I2S MCLK / BCLK / WS | 38 / 39 / 41 |
| I2S DOUT / DIN | 42 / 40 |
| Ses yükselteci PA | 46 |
| Ekran CS / CLK / RESET | 10 / 11 / 8 |
| Ekran QSPI D0 / D1 / D2 / D3 | 12 / 13 / 14 / 15 |

Hoparlörün pakette bulunması, konektör uyumu ve empedansı fiziksel kart/şema üzerinden teyit edilmeli. Dokunmatik panel codec I2C hattında (SDA 47 / SCL 48), adres 0x15, reset GPIO 7.

Derleme hedefi ESP-IDF **6.1**, ESP32-S3. Manifest aralığı `>=6.1.0,<7.0.0`. v0.1 belgeleri 5.5.2 diyordu; proje 6.1'e taşındı, 5.5 ile derleneceği artık doğrulanmıyor. IDF 6.0'da eski `driver` bileşeni `esp_driver_*` başlıklarını dışarı açmadığı için `esp_codec_dev` en az 1.6.2 olmalı; kök `CMakeLists.txt` henüz taşınmamış bileşenler için köprü kuruyor.

| Bileşen | Manifest şartı |
|---|---|
| ESP-SR | `2.5.3` |
| esp_codec_dev | `^1.6.2` (1.3.x IDF 6'da derlenmiyor) |
| esp_lcd_sh8601 | `^1.0.0` |
| esp_websocket_client | `^1.5.0` |
| LVGL | `~9.2.2` |

Kesin çözülmüş sürümler için `dependencies.lock` korunmalı. CPU 240 MHz, octal PSRAM 80 MHz ve USB Serial/JTAG konsolu yapılandırılmıştır.

## 6. Ayarlar, sınırlar ve API durumu

| Ayar | Mevcut varsayılan/davranış |
|---|---|
| Servis | OpenAI Realtime (varsayılan) veya Gemini Live |
| Model kimliği | `gpt-realtime-2.1` (ucuz seçenek: `gpt-realtime-2.1-mini`) |
| Akıl yürütme | `reasoning.effort = low`; boş bırakılırsa alan gönderilmez |
| Web araması | Açık; Gemini'de `googleSearch`, OpenAI'de uzak MCP sunucusu |
| Bağlam taşıma | 4 tur; 0 kapatır |
| Oturum yenileme | OpenAI 55. dakikada, Gemini 9. dakikada, cihaz boştayken |
| Yeniden bağlanma | 2 → 60 saniye artan gecikme |
| Ses | `marin` |
| Uyandırma | WakeNet9 `wn9_hiesp` (“Hi ESP”), eşik model varsayılanı |
| Diğer giriş yolları | Ekrana dokunma, BOOT düğmesi |
| Takip penceresi | 6 saniye |
| Mikrofon | 16 kHz PCM16, varsayılan sol I2S slot, 24 dB kazanç |
| Hoparlör seviyesi | %65 |
| Ekran yönü | 180° |
| Konuşmaya başlama beklemesi | 5 saniye; ses yoksa kayıt gönderilmez |
| Konuşma sonu sessizliği | 1000 ms |
| Maksimum kayıt | 20 saniye; aşılırsa kesilmiş kayıt gönderilmez |
| Yanıt sınırı | 400 çıktı tokenı; alınan seste 60 saniyelik yerel tavan |
| Yanıt bekleme | 90 saniye toplam; 20 saniye hareketsizlik sınırı |
| Başarısız soru | Otomatik tekrar gönderilmez |

Model kimliği ve ses adı **kaynakta yapılandırılan değerlerdir**; bu handoff sırasında servis kataloğu yeniden doğrulanmadı. Gerçek API hesabında model erişimi, oturum şeması ve ses uyumu ilk canlı testte doğrulanmalı. `model_not_found`, yetkilendirme ve kota hataları birbirinden ayrılmalı.

Önceki görüşmedeki aylık maliyet tahminleri bu belgenin doğrulanmış bütçesi değildir. Gerçek kullanım ve güncel API fiyatlarıyla yeniden hesaplanmalı. Mevcut ChatGPT aboneliğinin API kullanımını karşıladığı varsayılmamalı.

Prototip API anahtarını `menuconfig` üzerinden firmware'e gömer. Gerçek `sdkconfig`, binary ve anahtar paylaşılmamalı. TLS sertifika doğrulaması ve NTP açıktır. Ürünleşmede cihaz kimliği ve kısa ömürlü erişim sağlayan backend ayrı iş kalemidir.

## 7. Derleme ve ilk çalıştırma

ZIP'i açıp `ceko` klasöründe ESP-IDF 6.1 terminalini kullan:

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Port yer tutucudur; gerçek cihaz portuyla değiştir. İlk yükleme **tam `flash`** olmalı; konuşma modeli de yüklenir. Yalnızca `app-flash` yeterli değildir.

Önce `Ceko > Offline face animation demo` ile donanım yüzünü dene. Bu mod Wi-Fi/API/mikrofon başlatmaz. Normal kullanım için demoyu kapat, 2.4 GHz Wi-Fi bilgilerini ve kendi API anahtarını girip yeniden derle/yükle. Bilgisayar yüz önizlemesi: `docs/face-preview.html`.

## 8. Doğrulama kanıtı ve sınırları

`docs/VALIDATION.md` önceki geliştirme sırasında şunları kaydediyor:

- ESP-IDF 5.5.2 ile tam firmware, bootloader, bölüm tablosu ve konuşma modeli üretildi. **Bu kayıt 6.1 geçişinden öncedir ve artık geçerli sayılmamalı.**
- ELF içinde `speech_start`, `realtime_start`, `face_start`, `board_audio_init` sembolleri doğrulandı.
- ASan/UBSan ile kayıt sınırları, parçalı WebSocket akışı, örnekleme dönüşümü ve RMS testleri geçti. LeakSanitizer ortam kısıtı nedeniyle kapalıydı; sızıntı testi yapılmış sayılmaz.
- Yüz önizlemesinde JavaScript sözdizimi kontrolü geçti; tarayıcı görsel testi yapılmadı.

Bu handoff hazırlanırken kaynaklar ve doğrulama belgesi okundu, aşağıdaki binary boyutları dosyalardan yeniden kontrol edildi. Derleme ve testler tekrar çalıştırılmadı.

| Çıktı | Boyut | Bölüm |
|---|---:|---:|
| `build/ceko.bin` | 3.308.608 bayt | 4.194.304 bayt; yaklaşık %21 boş |
| `build/srmodels/srmodels.bin` | 2.761.093 bayt | 4.128.768 bayt |

Derlemede gerçek olmayan, boş olmayan bağlantı ayarları kullanıldığı belgelenmiş. Kaynak ZIP'ine bu yerel ayarlar/binary'ler dahil edilmediği belirtiliyor. Kullanıcı kendi ayarlarıyla yeniden derlemeli. Mevcut bölüm düzeninde OTA slotu yoktur.

**Henüz yapılmayanlar:** karta flash; fiziksel ekran/mikrofon/hoparlör kontrolü; Türkçe uyandırma doğruluğu; canlı OpenAI çağrısı; Türkçe yanıt kalitesi/gecikme ölçümü; uzun süreli bellek ve bağlantı testi.

## 9. Sonraki geliştiricinin iş sırası

1. Kaynak ZIP'i aç; README, VALIDATION ve bu belgeyi oku. Mevcut mimariyi koruyarak IDF 6.1 ile derlemeyi yeniden üret.
2. Offline demo yükle. Ekran yönü/renkleri, göz kırpma ve ağız görünümünü kullanıcıyla değerlendir.
3. Mikrofon verisi ve hoparlör çıkışını ayrı ayrı doğrula. Ses yoksa API veya wake eşiğini değiştirmeden önce codec/slot/kazancı kontrol et.
4. “Hey ceko”yu farklı mesafe ve gürültü koşullarında dene; deneme sayısı, başarılı algılama ve yanlış uyanmaları kaydet. Önce sessiz/yakın test, sonra gerçek masaüstü ortamı.
5. Canlı API hesabıyla kısa, güncel bilgi gerektirmeyen bir soru sor. Model erişimi, oturum kabulü, ses yanıtı ve tekrar IDLE'a dönüşü doğrula.
6. Sessiz uyandırma, uzun kayıt, Wi-Fi kesintisi, hatalı anahtar/kota ve yanıt zaman aşımı akışlarını dene. Yanıt sırasında yeniden uyanmama ve ağız hareketinin durması kontrol edilmeli.
7. Tekrarlı konuşmalarda serbest heap/PSRAM ve ilk ses gecikmesini kaydet. Sonuçları `docs/VALIDATION.md` içine ekle; ancak ölçülen özellikleri “çalışıyor” olarak işaretle.

Donanımdan bağımsız test komutu:

```sh
bash tools/test.sh
# Yalnızca ptrace ortamında LeakSanitizer çalışmıyorsa:
ASAN_OPTIONS=detect_leaks=0 bash tools/test.sh
```

Sonraki sürüm adayları: güvenilir özel uyandırma modeli; ilk hece kaybını önleyen tamponlama; AEC ve söz kesme; backend anahtar yönetimi; OTA; yerel model sağlayıcısı. Sohbet hafızası ve web araması v0.2'de eklendi ancak canlı doğrulaması yapılmadı.

## 10. Yeni oturuma verilecek başlangıç talimatı

> Ekli Ceko ZIP'ini ve CEKO_HANDOFF.md dosyasını devral. ESP-IDF 6.1, Waveshare ESP32-S3-Touch-AMOLED-1.32 ve v0.2'deki kalıcı oturumlu ağ katmanıyla devam et. Bas-konuş istemiyorum; “hey ceko” ile uyanmalı, göz kırpan iki göz ve sesle hareket eden ağız olmalı. Repo şimdilik açma. Kaynak ve doğrulama belgesini incele; ilk öncelik fiziksel ekran/ses, deneysel MultiNet uyandırması ve gerçek API bağlantısını doğrulamak. Derleme başarısını donanım başarısı olarak sunma. Model adını, pinleri veya çalışmayan bir özelliği tahmin ederek değiştirme; bulguyu kaydet ve gerekli değişikliği uygula.

## Referans izi

- Ürün: https://market.samm.com/esp32-s3-132-inc-amoled-dokunmatik-ekran-gelistirme-karti
- Üretici örnekleri: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.32
- README'deki üretici referans commit'i: `b3d6546e39db6287bc643eb0b0ad796e0ff61315`
- Bu belgenin teknik dayanağı: mevcut proje kaynakları, `README.md`, `docs/VALIDATION.md`, `main/Kconfig.projbuild`, manifest ve yerel binary boyutları. Yeni bir dış kaynak/API doğrulaması yapılmadı.
