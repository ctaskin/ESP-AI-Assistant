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

## İlk fiziksel çalıştırmadan çıkan iki hata (seri log ile)

- **TLS:** `esp-x509-crt-bundle: No matching trusted root certificate found`. Sunucu zinciri
  ölçüldü: `api.openai.com <- GTS WE1 <- GTS Root R4 <- GlobalSign Root CA`. ESP-IDF 6.1
  paketindeki (`components/mbedtls/esp_crt_bundle/cacrt_all.pem`, 145 sertifika)
  `GTS Root R4` var, çapraz imzayı atan eski `GlobalSign Root CA` yok. Seçenek kapalıyken
  paket sahte bir CA zinciri kurup yalnızca son sertifikanın vericisini arıyor; açıkken
  `mbedtls_ssl_conf_ca_cb` ile her seviyede aday kök sorulup `GTS Root R4` bulunuyor.
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y` yapıldı. **Kartta doğrulandı:**
  sonraki çalıştırmada sertifika hatası kayboldu ve WebSocket el sıkışması tamamlandı.
- **Kaybolan ayar:** Bu hata daha önce menuconfig ile çözülmüştü, ancak ayar yalnızca
  izlenmeyen `sdkconfig` dosyasındaydı; hedef düzeltmesi için o dosya silinince geri geldi.
  Ayar artık `sdkconfig.defaults` içinde ve kapalıysa derleme başında uyarı veriliyor.
- **Watchdog:** `task_wdt: IDLE1 (CPU 1)` ve `AFE(FEED) ringbuffer full` → MultiNet7 her
  çerçevede çalıştığı için çekirdek 1 doluyor ve tanıma gerçek zamanın gerisine düşüyordu.
  Tanıyıcı artık yerel VAD konuşma duyduğunda çalışıyor; 300 ms ön tampon ile ilk hece
  korunuyor, 1 saniye sessizlikten sonra duruyor. `wake_gate` modülü host testleriyle
  kapsandı. Gerçek algılama başarısı ve konuşma sırasındaki çekirdek yükü **ölçülmedi**.
- Wi-Fi, NTP, ES8311/I2S başlatma, MultiNet model yüklemesi ve yüz görevleri logda
  sorunsuz göründü. Ses giriş/çıkış kalitesi ve uyandırma başarısı hâlâ denenmedi.

## TLS sonrası çıkan durum (aynı gün, ikinci çalıştırma)

- **`session.update` reddediliyor:** `error code=invalid_value`. Hangi alanın reddedildiği
  loglanmıyordu; hata çıktısına artık `type`, `code`, `param` ve servis mesajı ekleniyor
  (ses içeriği değil, yalnızca şema bilgisi). Kurulum ayrıca kademeli hale getirildi:
  araçsız → akıl yürütmesiz → transkripsiyonsuz. Reddedilen alanın hangisi olduğu
  **henüz bilinmiyor**; en olası aday, belgelenmemiş hosted `web_search` aracıdır ve
  artık varsayılan olarak istenmiyor.
- **Saat beklemesi:** İlk bağlantı ancak ~36. saniyede denendi. SNTP, arayüz adres almadan
  önce ilk isteğini gönderip geri çekiliyordu; adres gelince `esp_sntp_restart()` çağrılıyor.
  Ayrıca bekleme artık sessiz değil: `waiting: wifi=... clock=...` satırı basılıyor.
- **AFE taşması sürüyor:** Konuşma sırasında `Ringbuffer of AFE(FEED) is full` yine çıkıyor,
  yani MultiNet konuşurken hâlâ gerçek zamanın gerisinde. Tanıyıcı tamponu iç RAM'e alındı;
  bu tek başına yetmezse kalıcı çözüm WakeNet aşamasıdır. Watchdog uyarısı kesildi.

## Web araması

Kartta doğrulanan: `session open` sonrası sohbet bağlamı turlar arasında korunuyor.
Arama ise hiç çalışmıyordu, çünkü OpenAI tarafında hiçbir araç gönderilmiyordu: hosted
`web_search` bu hesapta `invalid_value` ile reddedilmişti ve MCP adresi boştu.

Artık `CEKO_OPENAI_MCP_URL` varsayılan olarak Exa'nın barındırdığı anahtarsız uç noktaya
işaret ediyor ve araç bağlıyken model talimatına aramayı kullanmasını söyleyen bir cümle
ekleniyor. MCP çağrı olayları (`mcp_list_tools`, `response.mcp_call.*`) loglanıyor.
**Kartta henüz denenmedi**; Exa uç noktasının OpenAI tarafından kabul edildiği ve gerçek
bir aramanın döndüğü doğrulanmalı.

## claude/wizardly-albattani-0tc3dm dalının birleştirilmesi

Paralel bir dalda yapılmış ve bu dalda bulunmayan işler tek tek taşındı:

- **WakeNet9 `wn9_hiesp`** sürekli uyandırma. Bu daldaki MultiNet7 + VAD kapısı çözümü
  (`wake_gate.c`) tamamen kaldırıldı; MultiNet artık hiç kullanılmıyor. `sdkconfig.defaults`
  buna göre değişti (`CONFIG_SR_WN_WN9_HIESP=y`, `CONFIG_SR_MN_EN_NONE=y`) ve MultiNet
  yüzünden konulan `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n` geri alındı.
- **Dokunmatik panel** (`main/touch.c`, codec I2C hattı, 0x15) ve `board_i2c_bus()`.
- **`ceko_state_try_listen()`**: IDLE dışından kayıt açılmasını engelleyen atomik geçiş.
  Uyandırma, dokunma ve BOOT düğmesi bu kapıdan geçiyor.
- **Cevap sonrası takip penceresi** (6 sn) ve `capture_gate_init`'in bekleme parametresi.
- **Bağlantı hatası sınıflandırması** (401/403/404/429 ve TLS ayrımı).
- **IDF 6 köprüsü** `__COMPONENT_REQUIRES_COMMON` ile genel hale getirildi; bu dalın yalnızca
  iki bileşeni hedefleyen köprüsü kaldırıldı. `espressif/cjson` açık bağımlılık oldu, `lwip`
  `REQUIRES` listesine eklendi.
- **Alınmayan tek şey:** `certs/GlobalSign_Root_CA.pem` ve
  `CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE`. O dal TLS'i, Mozilla listesinden çıkarılmış
  1998 tarihli kökü pakete geri ekleyerek çözmüş. Bu dalda sorun
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` ile çözüldü: zincirdeki
  `GTS Root R4` zaten pakette ve güncel. Emekliye ayrılmış bir kökü yeniden güvenilir
  yapmamak için o yol tercih edilmedi; kartta doğrulandı.

## Üçüncü çalıştırma: oturum açıldı, giriş yolu yok

- **Doğrulandı:** `realtime: session open (openai)` — TLS, `session.update` ve tüm
  seçenekler (kademe 0, yani `reasoning.effort` ve transkripsiyon dahil) kabul edildi.
  Watchdog ve AFE taşma uyarıları da bu çalıştırmada çıkmadı.
- **Uyandırma hiçbir çalıştırmada tetiklenmedi.** Handoff'ta baştan işaretlenen risk
  gerçekleşti: İngilizce MultiNet7 ile `hey jeko` yazımı Türkçe telaffuzu yakalamıyor
  olabilir; mikrofon verisi de henüz bağımsız doğrulanmadı. Bu yüzden beş saniyede bir
  örnek/konuşma/tepe seviye/model parçası sayıları loglanıyor ve eşiğin altındaki
  MultiNet adayları da yazdırılıyor.
- **Dokunmatik yok:** Bu firmware'de dokunmatik sürücüsü hiç uygulanmadı; "touch
  dinlemeyi başlatıyor" doğru değil. Bring-up için BOOT (GPIO0) düğmesi eklendi.
- Tanıyıcı kapısı artık yalnızca VAD'a bağlı değil: mikrofon seviyesi eşiği de kabul
  ediyor, böylece VAD hiç açılmasa bile model çalışabiliyor.

## ESP-IDF 6.1 geçişi ve repoya girmiş yanlış derleme durumu

Proje ESP-IDF **6.1** ile derleniyor (`.vscode/settings.json` ve `dependencies.lock`
bunu gösteriyor). v0.1 belgelerindeki "ESP-IDF 5.5.2" kayıtları geçersizdir.

Derlemeyi durduran üç şey repodaydı:

- `main/idf_component.yml` `esp_codec_dev ~1.3.4` istiyordu. 1.3.x yalnızca eski
  `driver` bileşenini istiyor; IDF 6.0 bu bileşenden `esp_driver_gpio` /
  `esp_driver_i2c` başlıklarını dışarı açmayı bıraktığı için derleme
  `driver/gpio.h: No such file or directory` ile duruyordu. Kısıt `^1.6.2` yapıldı.
- İzlenen `sdkconfig` dosyası `CONFIG_IDF_TARGET="esp32"` içeriyordu; derleme yanlış
  yongaya gidiyordu. Dosya artık izlenmiyor, kök `CMakeLists.txt` yanlış hedefte
  derlemeyi durduruyor.
- Manifest `idf: ">=5.5.0,<6.0.0"` diyordu; `>=6.1.0,<7.0.0` yapıldı.

Kök `CMakeLists.txt` ayrıca IDF 6'da hâlâ eski düzene güvenen managed bileşenlere
eksik `esp_driver_*` başlık yollarını veriyor. Bu köprünün ve yeni `esp_codec_dev`
sürümünün gerçekten derlendiği **bu ortamda doğrulanamadı**; ESP-IDF burada kurulu değil.

## v0.1 sırasında yapılanlar

- ESP-IDF 5.5.2 / ESP32-S3 için tüm uygulama C dosyaları derlendi (6.1 geçişinden önce; tekrarlanmalı).
- Donanımdan bağımsız C testleri AddressSanitizer + UndefinedBehaviorSanitizer altında geçti.
  Bu çalışma ortamında ptrace nedeniyle LeakSanitizer kapatıldı; sızıntı testi yapılmış sayılmaz.
- Testler: sessizlikte kayıt göndermeme, konuşma bitişi, uzun kayıt reddi, WebSocket mesaj
  parçalama/birleştirme, devam frame'leri arasında ping, sıra/boyut hatası reddi,
  resampler durumunun paket sınırlarında korunması, 16↔24 kHz örnek sayısı,
  Nyquist üstü frekans bastırma, RMS ve çıkış tampon kapasitesi kontrolü.
- Tarayıcı önizlemesinin JavaScript sözdizimi `node --check` ile doğrulandı.
  Bu ortamda Chromium olmadığı için görsel ekran görüntüsü testi yapılmadı.

## Tam firmware derlemesi — başarılı

ESP-IDF 5.5.2 ve o günkü kilit dosyasıyla `idf.py build` tamamlandı. Bu kanıt 6.1 geçişinden öncedir.
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
