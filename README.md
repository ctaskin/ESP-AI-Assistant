# Ceko v0.1 — ESP-IDF sesli masaüstü asistanı

**Kart:** Waveshare ESP32-S3-Touch-AMOLED-1.32, 466×466, 8 MB Flash / 8 MB PSRAM, ES8311.
**Hedef:** ESP-IDF 6.1; Arduino kullanılmaz. Repo oluşturulmadı.

## Davranış

1. Siyah ekranda turkuaz iki göz aralıklarla kırpılır, hafifçe etrafa bakar.
2. Beklerken ses yalnızca cihazdaki AFE ve deneysel komut tanıyıcıda işlenir; internete gönderilmez.
3. “Hey ceko” algılanınca gözler büyür ve yeşile döner. `Dinliyorum` görünür.
4. Bundan sonraki konuşma alınır; 1 saniyelik sessizlikte kayıt tamamlanır.
5. Ses OpenAI Realtime mini'ye TLS üzerinden gönderilir. Yanıt geldikçe hoparlörden çalınır.
6. Ağız, **çalınan sesin RMS şiddetine** göre açılıp kapanır. Fonem/dudak eşleştirmesi değildir.
7. Cevap bitince tekrar “hey ceko” bekler. Her çağrı bağımsızdır; v0.1'de sohbet hafızası yoktur.

**Bas-konuş yok.** API'nin `commit` komutunu yerel sessizlik algılama tetikler.
Bilgisayar/bridge açık kalmadan, kart Wi-Fi üzerinden doğrudan OpenAI'ye bağlanır.

## Dinlemeye geçiş: iki yol

**Ekrana dokun.** Boştayken ekrana bir kez dokunmak doğrudan dinleme moduna geçirir; gözler
yeşile döner. Güvenilir yol budur — dokunmatik panel deterministiktir, uyandırma sözcüğü
değildir. Dokunma yalnızca boştayken kabul edilir: Ceko düşünürken veya konuşurken kayıt
tamponu ağ görevine ait olduğundan dokunuş yok sayılır ve seri loga düşer.

Dokunmatik panel üretici referansındaki değerlerle sürülüyor: codec ile aynı I2C hattı
(SDA 47 / SCL 48), adres `0x15`, reset GPIO 7. Açılışta panel yoklanır; log satırı
`Touch panel answered at 0x15` ise panel yanıt veriyor demektir. `Touch panel silent`
görüyorsan dokunmatik uyandırma çalışmaz. `menuconfig > Ceko > Start listening when the
screen is touched` ile kapatılabilir.

**"Hi ESP" de.** Uyandırma sözcüğü WakeNet9 `wn9_hiesp` modelidir; İngilizce "Hi, ESP",
yani *hay es pi*. Espressif'in hazır modeli, ayrı eğitim gerektirmez. Gözler yeşile dönünce
sorunu sor. Ayrıntı: "Uyandırma sözcüğü".

## Önce bilmen gereken iki sınır

- **Uyandırma sözcüğü Türkçe değil:** Sesli aktivasyon hazır WakeNet9 `wn9_hiesp` modeliyle,
  yani "Hi, ESP" ile yapılır. Türkçe "Hey Ceko" için gerçek bir WakeNet modeli gerekiyor ve o
  Espressif'in ayrı model özelleştirme süreciyle üretiliyor; yalnızca metin yazmak yetmiyor.
  Bu, açık bir sonraki iş kalemidir.
- **Yarı çift yönlü:** Ceko cevap verirken mikrofon işlenip atılır. Kendi sesine uyanmaz;
  fakat konuşurken sözünü kesme yoktur. Bu sürümde akustik yankı giderme kapalıdır.

Uyandırma sözcüğünü söyledikten sonra **gözler yeşile dönünce konuş**. Aynı nefeste devam
edilen sorunun ilk hecesi kaçabilir; ön ses tamponlaması bu sürümde yok.

### Arka arkaya soru sorma

Ceko cevabını bitirdikten sonra **6 saniye dinlemede kalır** — gözler yeşil durur ve doğrudan
ikinci soruyu sorabilirsin, uyandırma sözcüğünü tekrarlaman gerekmez. Bir şey demezsen kendi
kendine boşta moduna döner ve yeniden "Hi ESP" gerekir.

Süre `menuconfig > Ceko > Keep listening after an answer` ile değiştirilir; `0` bu davranışı
kapatır ve her soru için uyandırma gerekir. Seri log hangi pencerenin açıldığını yazar:

```
I (…) speech: Listening (new question, 5 s to start speaking)
I (…) speech: Listening (follow-up, 6 s to start speaking)
```

İki sınır: her tur ayrı bir istektir, **sohbet geçmişi tutulmaz** — ikinci soru birincinin
bağlamını bilmez. Ve başarısız bir istekten sonra pencere açılmaz, doğrudan boşta moduna
dönülür.

## Kurulum — Mac / VS Code

1. ZIP'i aç, `ceko` klasörünü VS Code ile aç.
2. ESP-IDF eklentisinde **6.1** kurulumunu seç. `ESP-IDF: Open ESP-IDF Terminal` aç.
3. Proje klasöründe:

```sh
idf.py set-target esp32s3
idf.py menuconfig
```

4. **Ceko** menüsüne gir:
   - `Wi-Fi SSID`: 2.4 GHz ağ adı.
   - `Wi-Fi password`: ağ şifresi.
   - `OpenAI API key`: ayrı API hesabının anahtarı. ChatGPT Plus dahil değildir.
   - `OpenAI Realtime model ID`: varsayılan `gpt-realtime-2.1-mini`.
   - Ses: `marin`, hoparlör seviyesi `%65`, mikrofon kazancı `24 dB`.
5. Kaydet/çık ve derle:

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

`XXXX` yerine eklentinin bulduğu gerçek portu yaz. İlk yüklemede **tam `flash`** yap:
WakeNet model bölümü de yüklenir. Yalnızca `app-flash` kullanma. Seri monitörden çıkış: `Ctrl+]`.
Kart görünmüyorsa BOOT'a basılı tutarak USB'ye bağla, ardından bırak.

Kullanılabilir OpenAI model adları hesap bazında değişebilir. `model_not_found`, `invalid_api_key`
veya kota hatası seri logda kod olarak görünür. Burada gerçek API hesabıyla doğrulama yapılmadı.

### Hoparlör

ES8311 mikrofon/ses devresi kartta bulunur. Kartın kendi hoparlör konektörüne uygun hoparlör
bağlı olmalı. Paketinde hoparlör yoksa uygun parça gerekir; konektör ve yük empedansı kartın
şeması/ürün revizyonundan kontrol edilmelidir. GPIO'ya doğrudan hoparlör bağlanmaz.

### API anahtarının yeri

Bu kişisel prototipte anahtar `menuconfig` üzerinden firmware'e gömülür; flash'tan çıkarılabilir.
Bu yüzden firmware ve gerçek `sdkconfig` dosyanı paylaşma. `.gitignore` hazırdır; daha sonra
repo açınca `sdkconfig`, `build` ve `managed_components` eklenmez. Kaynak paketi anahtar içermez.
Ürünleşmede cihaz kimlik doğrulaması ve kısa ömürlü token veren backend eklenmeli.
TLS sertifika kontrolü açıktır, sistem saati NTP ile ayarlanır. Sertifika kontrolünü kapatma.

### Derleme sorunları

Proje ESP-IDF **6.1** ve **esp32s3** hedefiyle yapılandırılmıştır. Başka bir ortamda
başlatılırsa derleme, yapılandırma aşamasında açık bir hatayla durur — eskiden olduğu gibi
bağımlı bir bileşenin içinde yüzlerce adım sonra değil.

**`driver/gpio.h: No such file or directory`** — `managed_components/espressif__esp_codec_dev`
içinde derlerken görülür. IDF 6'da eski `driver` bileşeni bölündü: artık yalnızca
`i2c` / `touch_sensor` / `twai` başlıklarını aktarıyor, `esp_driver_gpio`'yu ise
`PRIV_REQUIRES` olarak tutuyor. Hâlâ `REQUIRES driver` yazıp IDF 5 şemsiyesine güvenen
üçüncü taraf bileşenler bu yüzden `driver/gpio.h`, `driver/i2s_std.h` ve
`driver/i2c_master.h` bulamıyor. Kök `CMakeLists.txt` ayrılmış sürücü bileşenlerini ortak
gereksinim listesine ekleyerek bu başlıkları geri veriyor; ayrıca `esp_codec_dev` sürüm
aralığı `^1.3.4`'e genişletildi ki çözücü IDF 6'ya uygun daha yeni bir sürüm seçebilsin.
Bağımlılıklar IDF 6'ya tümüyle geçtiğinde kökteki bu blok kaldırılabilir.

**Yanlış hedef** — `set-target` çalıştırılmadan derlenirse hedef `esp32` kalabilir. `sdkconfig`
varsa `sdkconfig.defaults` **uygulanmaz**, dolayısıyla oradaki `CONFIG_IDF_TARGET="esp32s3"`
satırı da devreye girmez. Belirtileri: derleme çıktısında `xtensa-esp32-elf-gcc` ve
`Building ESP-IDF components for target esp32`, ayrıca
`unknown kconfig symbol 'SPIRAM_MODE_OCT'` uyarısı (ESP32 klasikte oktal PSRAM yoktur).
ESP32 hedefinde SH8601 QSPI ekran ve ESP-SR modelleri zaten çalışmaz.

Her iki durumda da temiz başlangıç:

```sh
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32s3
idf.py menuconfig   # Ceko ayarlarını yeniden gir
idf.py build
```

**`idf.py fullclean` kullanma.** Yukarıdaki iki kontrol derlemeyi `project()` çağrısından
önce durdurur; bu noktada `build/` geçerli bir `CMakeCache.txt` olmadan kalır ve `fullclean`
böyle bir dizini silmeyi reddeder:

```
Directory '.../build' doesn't seem to be a CMake build directory.
Refusing to automatically delete files in this directory.
```

`set-target` kendi bağımlılığı olarak `fullclean` çağırdığı için aynı hata `idf.py set-target`
sırasında da çıkar. VS Code ESP-IDF eklentisi de aynı durumda kendi uyarısını verir:
`There is no CMakeCache.txt. Please try to delete the build directory manually.`

Nedeni: cmake yapılandırması hata verdiğinde `idf.py`, yarım kalmış bir önbellek bırakmamak
için `build/CMakeCache.txt` dosyasını **bilerek siler**
(`tools/idf_py_actions/tools.py`, "don't allow partially valid CMakeCache.txt files").
Geriye dolu ama önbelleksiz bir `build/` kalır ve `fullclean` tam olarak bunu silmeyi
reddeder — kendini besleyen bir kilitlenme.

Tek çıkış yolu dizini **terminalden elle silmektir**; eklentinin düğmeleri bunu yapmaz:

```sh
cd /path/to/ESP-AI-Assistant
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32s3
```

`sdkconfig` üretilen bir dosyadır ve repoda tutulmaz; hedefi ve anahtarları taşıdığı için
depoya girerse bir sonraki derlemeye yanlış hedefi dayatır. `dependencies.lock` IDF 6 geçişinde
silindi; ilk başarılı derlemede yeniden üretilecek ve **oluşan kilit işlenmelidir**.

### ESP-IDF 5.x'e dönüş

Desteklenmiyor. cJSON, IDF 6'da çekirdekten çıkarıldı ve `espressif/cjson` olarak ayrı bir
bağımlılık; IDF 5.x'te aynı başlığı çekirdekteki `json` bileşeni de verdiği için çakışır.
Kök `CMakeLists.txt` bu yüzden IDF 6'dan küçük sürümlerde derlemeyi durdurur.

### Uyandırma sözcüğü

Sesli aktivasyon **WakeNet9 `wn9_hiesp`** ile yapılır: İngilizce "Hi, ESP", yani *hay es pi*.
Model ESP-SR paketinde hazır gelir, ayrı eğitim gerektirmez.

Sözcük `menuconfig > ESP Speech Recognition > Load Multiple Wake Words (WakeNet9)` altından
seçilir. `sdkconfig.defaults` bunu `CONFIG_SR_WN_WN9_HIESP=y` ile ayarlar. Başka bir hazır
sözcüğe geçersen `main/ceko.h` içindeki `CEKO_WAKE_HINT` metnini de güncelle — ekranda o yazar.

> **Neden "hey ceko" değil.** Önceki sürüm İngilizce komut tanıyıcısı MultiNet'i sürekli
> dinleterek uyandırma sözcüğü gibi kullanıyordu. Espressif'in dokümanı MultiNet'in WakeNet
> cihazı uyandırdıktan **sonra** çalıştırılmasını söylüyor; sürekli dinletmek desteklenen bir
> kullanım değil ve pratikte hiç tetiklenmedi (logda tek bir aday bile çıkmadı). Türkçe
> "Hey Ceko" için gerçek bir WakeNet modeli gerekiyor; bu Espressif'in ayrı model özelleştirme
> süreciyle üretilir, metin yazmakla olmaz. Sonraki iş kalemi.

Açılışta model adı loglanır, tetiklendiğinde de:

```
I (…) speech: Ready: wake=wn9_hiesp, free internal=… PSRAM=… bytes
I (…) speech: Wake word detected (word index 1)
```

`W AFE_CONFIG: wakenet model not found` görüyorsan model seçili değil demektir; o durumda
yalnızca dokunmatik çalışır ve kod bunu açıkça loglar.

WakeNet yalnızca boştayken dinlenir; Ceko konuşurken kapatılır, böylece kendi sesiyle
uyanmaz.

Duyarlılık gerekirse `menuconfig > Ceko > WakeNet detection threshold percent` ile
değiştirilir. `0` modelin kendi eşiğini korur — varsayılan budur ve Espressif'in ayarladığı
değerdir. 40-99 arası bir değer yalnızca override etmek için verilir; düşürmek daha kolay
tetikler ama yanlış uyanmaları artırır.

### Mikrofon çalışmıyorsa

Seri monitör beş saniyede bir tepe seviye basar:

```
I (…) speech: Microphone peak level over 5 s: 46/100
```

Sürekli 0 ise mikrofon yolu ölü demektir. Uyandırma ayarlarına dokunma; önce codec, I2S slot
ve kazancı doğrula: `menuconfig > Ceko > Use right I2S microphone slot` ile slotu değiştir,
`Microphone gain dB` değerini artır.

### Bağlantı hataları

Bağlantı hatası artık sınıflandırılıyor; seri logda tek bir ayrıntı satırı var:

```
E (…) realtime: WebSocket error: type=1 http_status=0 tls_esp_err=ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED
      tls_stack_err=12288 tls_cert_flags=0x00000000 sock_errno=119
```

| Ekranda | Anlamı |
|---|---|
| `API anahtari reddedildi (401)` | Anahtar yanlış veya iptal edilmiş |
| `Erisim yok (403)` | Hesapta bu kaynağa yetki yok |
| `Model bulunamadi (404)` | `OpenAI Realtime model ID` bu hesapta yok |
| `Kota veya hiz siniri (429)` | Bakiye veya hız sınırı |
| `TLS: sertifika dogrulanamadi` | El sıkışma kök sertifikada düştü |

**`TLS: sertifika dogrulanamadi` / `No matching trusted root certificate found`:** bu sorun
teşhis edildi ve `certs/GlobalSign_Root_CA.pem` ile çözüldü. Özeti:

`api.openai.com` zincirinin tepesi, **GlobalSign Root CA tarafından çapraz imzalanmış** GTS
Root R4'tür. mbedTLS zincirin tepesindeki sertifikanın **verenini** arar; ESP-IDF 6.1 paketinde
GTS Root R1–R4 var ama klasik `GlobalSign Root CA` (1998) yok — Mozilla deposundan çıkarılmış.
Çapraz imza desteği de aramayı verene göre yaptığı için kurtarmıyor. Kök `certs/` altına
eklenip yerleşik paketin üzerine bindiriliyor. Ayrıntı: `certs/README.md`.

Mevcut bir `sdkconfig` ile derliyorsan `sdkconfig.defaults` **uygulanmaz**; seçeneği elle aç:

```
idf.py menuconfig
  Component config > ESP-TLS / mbedTLS > Certificate Bundle
    [*] Add custom certificates to the default bundle
        Custom certificate bundle path: certs
```

Başka bir sunucuda benzer bir hata alırsan zinciri şöyle çıkar ve eksik halkayı `certs/`
altına koy:

```sh
openssl s_client -showcerts -servername HOST -connect HOST:443 </dev/null 2>/dev/null |
  openssl crl2pkcs7 -nocrl -certfile /dev/stdin | openssl pkcs7 -print_certs -noout
```

Zincirin tepesindeki sertifikanın **issuer** satırı, pakette bulunması gereken köktür. Orada
kurum veya router adı görüyorsan sorun sertifika değil, ağın TLS'i araya girmesidir.

Dokunmatik uyandırma çalışıyor ama sözcük çalışmıyorsa, mikrofon ve kayıt zinciri sağlam
demektir; bağlantı hatası alıyorsan sorun uyandırmada değil ağ/TLS tarafındadır.

## Ayarlar

| Ayar | Varsayılan | Amaç |
|---|---|---|
| Uyandırma sözcüğü | `wn9_hiesp` ("Hi, ESP") | ESP-SR menüsünden seçilir, Ceko menüsünden değil |
| Uyandırma eşiği | Model varsayılanı (`0`) | 40-99 yalnızca override içindir |
| Dokunmatik uyandırma | Açık | Ekrana dokununca dinlemeye geçer |
| Sessizlik | 1000 ms | Sorunun bittiğine karar verme |
| Maksimum kayıt | 20 saniye | RAM ve kullanım sınırı; aşılırsa kayıt gönderilmez |
| Konuşma bekleme | 5 saniye | Uyandırmadan sonra ses yoksa ücretsiz beklemeye döner |
| Cevap sonrası dinleme | 6 saniye | Uyandırma sözcüğü olmadan ikinci soru; `0` kapatır |
| Yanıt sınırı | 400 token / 60 sn ses | Kısa cevap ve sınırlı kuyruk |
| Mikrofon I2S slot | Sol | Ses gelmezse donanım testinde sağ slot denenebilir |
| Ekran yönü | 180° | Üretici demo yönü; menüden değiştirilebilir |

Oturum yalnızca kaydın sonunda açılır; internet bağlantısı kurulurken konuşma kaybolmaz,
ancak ilk yanıt gecikmesine TLS/oturum açma süresi eklenir. V0.1 maliyet/akış basitliği tercihidir.
Beklemedeki ses, uyandırma çerçevesi ve cevap sırasındaki mikrofon sesi gönderilmez.
Başarısız istek otomatik tekrar gönderilmez; böylece aynı soruya çift ücret riski azaltılır.
Yanlış uyandırma sonrası gerçek konuşma algılanırsa o kayıt API'ye gönderilebilir.

## Görünümü API'siz dene

- Bilgisayarda `docs/face-preview.html` dosyasını aç. İnternet, mikrofon, anahtar gerekmez.
- Kartta `menuconfig > Ceko > Offline face animation demo` seçeneğini açıp derle/yükle.
  Bu modda Wi-Fi/API/mikrofon başlatılmaz; dört yüz durumu otomatik sırayla gösterilir.

## Dosyalar

| Dosya | Görev |
|---|---|
| `main/board.c` | Güç, ES8311 ve I2S ses sürücüsü |
| `main/face.c` | AMOLED, LVGL 9, göz ve ağız animasyonu |
| `main/touch.c` | Dokunmatik panel, dokununca dinlemeye geçiş |
| `main/speech.c` | Yerel AFE/VAD, WakeNet uyandırma ve kayıt |
| `main/realtime.c` | OpenAI WebSocket, ses kuyruğu, hata ve zaman aşımı |
| `main/capture_gate.c` | Konuşma/sessizlik ve kayıt süresi sınırları |
| `main/audio_math.c` | 16↔24 kHz FIR dönüşümü ve RMS |
| `main/ws_message.c` | Sınırlı boyutlu parçalı WebSocket mesaj birleştirme |
| `tests/test_core.c` | Donanımdan bağımsız sınır/akış testleri |
| `docs/VALIDATION.md` | Gerçekte yapılan kontroller ve kalan fiziksel testler |

## Test

C derleyicisi bulunan bilgisayarda:

```sh
bash tools/test.sh
```

AddressSanitizer/UndefinedBehaviorSanitizer ile kayıt sınırları, parçalı mesajlar ve ses
akışı doğrulanır. Ptrace kullanan ortamlarda LeakSanitizer çalışmıyorsa:
`ASAN_OPTIONS=detect_leaks=0 bash tools/test.sh` (sadece leak kontrolü kapanır).

## Kaynaklar

- [Waveshare kart örnekleri](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.32)
  — referans commit `b3d6546e39db6287bc643eb0b0ad796e0ff61315`.
- [ESP-SR komut tanıma](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html).
- [OpenAI Realtime konuşma olayları](https://developers.openai.com/api/docs/guides/realtime-conversations).
- [OpenAI WebSocket bağlantısı](https://developers.openai.com/api/docs/guides/voice-websockets).
- [GPT-Realtime-2.1 Mini](https://developers.openai.com/api/docs/models/gpt-realtime-2.1-mini).

Kart pinleri ve panel init değerleri üretici referansından doğrulandı. Uygulama kodu bu
prototip için yazıldı. ESP-IDF, ESP-SR, LVGL ve codec bileşenleri kendi lisansları altındadır;
model dosyaları otomatik indirilen Espressif bileşeninin parçasıdır.
