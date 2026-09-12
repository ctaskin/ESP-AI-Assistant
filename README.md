# Ceko v0.2 — ESP-IDF sesli masaüstü asistanı

**Kart:** Waveshare ESP32-S3-Touch-AMOLED-1.32, 466×466, 8 MB Flash / 8 MB PSRAM, ES8311.
**Hedef:** ESP-IDF 6.1; Arduino kullanılmaz.

## Davranış

1. Siyah ekranda turkuaz iki göz aralıklarla kırpılır, hafifçe etrafa bakar.
2. Beklerken ses yalnızca cihazdaki AFE ve WakeNet'te işlenir; internete gönderilmez.
3. **“Hi ESP”** algılanınca gözler büyür ve yeşile döner, `Dinliyorum` görünür.
   Ekrana dokunmak veya BOOT düğmesine basmak da aynı işi yapar.
4. Bundan sonraki konuşma alınır; 1 saniyelik sessizlikte kayıt tamamlanır.
5. Ses, **açılışta kurulmuş ve açık tutulan** WebSocket oturumundan gönderilir.
   Yanıt geldikçe hoparlörden çalınır.
6. Ağız, **çalınan sesin RMS şiddetine** göre açılıp kapanır. Fonem/dudak eşleştirmesi değildir.
7. Cevap bitince **6 saniyelik takip penceresi** açık kalır: devam eden soruda uyandırma
   sözcüğünü tekrarlamana gerek yok. Bir şey söylemezsen kendiliğinden beklemeye döner.
   **Sohbet aynı oturumda devam eder:** az önce konuşulan konuya atıf yapabilirsin.
   Bağlantı koparsa veya oturum yenilenirse son turların kısa özeti yeni oturuma taşınır.

**Bas-konuş yok.** Turun bittiğini yerel sessizlik algılama bildirir.
Bilgisayar/bridge açık kalmadan, kart Wi-Fi üzerinden doğrudan servise bağlanır.

**Servis seçimi:** `menuconfig > Ceko > Realtime voice service` ile OpenAI Realtime
veya Google Gemini Live seçilir. Karşılaştırma, fiyatlar ve gerekçe: `docs/PROVIDERS.md`.

### Web araması

Aramayı her iki durumda da **servis yapar**; kart ikinci bir bağlantı açmaz.

- **Gemini Live:** `googleSearch` aracı oturumda açık. Ek ayar yok.
- **OpenAI Realtime:** Uç noktada güvenilir bir yerleşik arama aracı yok; arama uzak bir
  **MCP sunucusu** üzerinden yapılır ve OpenAI o sunucuyu kendisi çağırır. Varsayılan
  `https://mcp.exa.ai/mcp` (Exa'nın barındırdığı uç nokta; hız sınırlı ücretsiz planda
  anahtar istemiyor). Modelin aramaya karar verdiği sorular bu servise ulaşır — istemiyorsan
  `menuconfig > Ceko > Remote MCP server URL` alanını boşalt, Ceko çevrimdışı kalır.
  Kendi MCP sunucunu (Tavily, Brave, kendi kurduğun) adres ve gerekiyorsa bearer token
  girerek kullanabilirsin.

Arama yapılan turlar **belirgin biçimde yavaştır**: servis, uzak sunucuyu çağırırken
saniyelerce hiçbir şey göndermez. Bu yüzden bir araç çalıştığında yanıt bekleme sınırı
45 saniyeye, araç etkinken 60 saniye sessizliğe / toplam 3 dakikaya çıkar; ekranda
`Ariyorum` görünür. Vazgeçildiğinde servise `response.cancel` gönderilir, böylece
kimsenin duymayacağı bir cevap üretilip faturalanmaz.

Çalıştığını seri logdan görürsün:

```
realtime: tool: mcp_list_tools.in_progress      (oturum acilirken arac listesi)
realtime: tool: response.mcp_call.in_progress   (soru sirasinda arama)
```

Araç bağlıyken model talimatına "güncel bilgi gerekirse aramayı kullan ve kaynağı söyle"
cümlesi ekleniyor; bağlı değilken eklenmiyor, böylece olmayan bir aracı kullanmaya
çalışmıyor. MCP sunucusu ulaşılamazsa `session.update` reddedilir ve kurulum bir kademe
geri düşüp **araçsız** devam eder — logda `session configured without optional fields`
satırı görünür.

## Önce bilmen gereken sınırlar

- **Uyandırma sözcüğü “Hi ESP”:** Espressif'in hazır WakeNet9 modeli (`wn9_hiesp`)
  kullanılıyor; sürekli dinlemek için tasarlanan motor budur. Türkçe “ceko” için eğitilmiş
  hazır model yok. Önceki sürümdeki MultiNet7 denemesi bırakıldı: MultiNet bir *komut*
  tanıyıcıdır, uyandırma sözcüğünden **sonra** çalışmak üzere tasarlanmıştır ve bu yongada
  sürekli çalıştırıldığında gerçek zamanı yakalayamıyordu (çekirdek 1 doluyor, AFE tamponu
  taşıyordu). Türkçe bir uyandırma sözcüğü istiyorsan Espressif'e özel model eğittirmek
  gerekir; bu ayrı bir iş kalemidir.
  Sözcük `menuconfig > ESP Speech Recognition > Load Multiple Wake Words` altından,
  eşiği `menuconfig > Ceko > WakeNet detection threshold` ile değiştirilir.
- **Üç giriş yolu:** uyandırma sözcüğü, ekrana dokunma, BOOT düğmesi. Üçü de yalnızca
  beklerken kabul edilir; Ceko düşünürken/konuşurken kayıt tamponu ağ görevine ödünç
  verilmiştir ve dokunuş yok sayılır.
- **Yarı çift yönlü:** Ceko cevap verirken mikrofon işlenip atılır ve WakeNet kapatılır;
  kendi sesine uyanmaz. Konuşurken sözünü kesme yoktur, akustik yankı giderme kapalıdır.
- **Gemini yolu doğrulanmadı:** Gemini Live mesaj alanları dokümantasyondan yazıldı,
  canlı bir hesapla denenmedi.

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
   - `Realtime voice service`: OpenAI Realtime (varsayılan) veya Gemini Live.
   - `OpenAI API key`: ayrı API hesabının anahtarı. ChatGPT Plus dahil değildir.
     Gemini seçersen `Google AI Studio API key` alanı gelir.
   - `OpenAI Realtime model ID`: varsayılan `gpt-realtime-2.1`. Ucuz seçenek:
     `gpt-realtime-2.1-mini`.
   - Ses: `marin`, hoparlör seviyesi `%65`, mikrofon kazancı `24 dB`.
5. Kaydet/çık ve derle:

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

**Proje ESP-IDF 6.1 ile derlenir** (`main/idf_component.yml`: `>=6.1.0,<7.0.0`).
Hedef yonga esp32s3 değilse derleme en başta açık bir mesajla durur; ayrıntı aşağıdaki
"Derleme sorunları" bölümünde.

`XXXX` yerine eklentinin bulduğu gerçek portu yaz. İlk yüklemede **tam `flash`** yap:
WakeNet model bölümü de yüklenir. Yalnızca `app-flash` kullanma. Seri monitörden çıkış: `Ctrl+]`.
Kart görünmüyorsa BOOT'a basılı tutarak USB'ye bağla, ardından bırak.

Kullanılabilir OpenAI model adları hesap bazında değişebilir. `model_not_found`, `invalid_api_key`
veya kota hatası seri logda kod olarak görünür. Burada gerçek API hesabıyla doğrulama yapılmadı.

### Derleme sorunları

**`fatal error: driver/gpio.h` veya `driver/i2c_master.h` (esp_codec_dev içinde)**
ESP-IDF 6.0'da eski `driver` bileşeni `esp_driver_gpio`, `esp_driver_i2c` gibi
bileşenleri artık dışarı açmıyor; bunlar `PRIV_REQUIRES` içine taşındı. Yalnızca
`driver` isteyen `esp_codec_dev` 1.3.x bu yüzden başlıkları bulamıyor. Repoda iki
düzeltme var: `main/idf_component.yml` artık `esp_codec_dev ^1.6.2` istiyor (bu sürüm
IDF 5.3 ve üstünde `esp_driver_*` bileşenlerini kendi seçiyor) ve kök `CMakeLists.txt`,
henüz taşınmamış bileşenlere eksik başlık yollarını veriyor.

Eski çözümlenmiş sürümleri ve yanlış hedefi atmak için:

```sh
rm -rf build managed_components dependencies.lock sdkconfig
idf.py set-target esp32s3
idf.py build
```

**`esp-x509-crt-bundle: No matching trusted root certificate found`**
`api.openai.com` zinciri şöyle bitiyor:

```
api.openai.com  <-  GTS WE1  <-  GTS Root R4  <-  GlobalSign Root CA
```

Son halka çapraz imza: sunucu, `GTS Root R4`'ün `GlobalSign Root CA` tarafından imzalanmış
sürümünü gönderiyor. O eski kök (1998 tarihli R1) Mozilla listesinden çıkarıldığı için ESP
sertifika paketinde **yok**; `GTS Root R4` ise **var**. Seçenek kapalıyken paket yalnızca
zincirin son sertifikasının vericisine bakar, onu bulamaz ve el sıkışmayı keser.
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y` açıldığında mbedTLS her seviyede
paketten aday kök sorar, `GTS Root R4`'ü bulur ve fazladan çapraz halkayı yok sayar.
Bu seçenek ESP-IDF'de varsayılan olarak **kapalıdır**; `sdkconfig.defaults` açıyor.

`GlobalSign Root CA`'yı elle eklemeye çalışma: dağıtımdan çıkarılmış bir kökü yeniden
güvenilir yapmak, çözümü değil güvenlik zafiyetini eklemek olur.

**Önemli:** `sdkconfig` zaten varsa `sdkconfig.defaults` onu ezmez. Mevcut kurulumda ayarı
şöyle aç (Wi-Fi ve anahtar ayarların korunur):

```sh
sed -i '' 's/^# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY is not set$/CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y/' sdkconfig
grep CROSS_SIGNED sdkconfig      # =y görmelisin
idf.py build
```

Aynısını `idf.py menuconfig > Component config > mbedTLS > Certificate Bundle` altından da
yapabilirsin. Derleme başında bu uyarı görünürse ayar hâlâ kapalıdır.

**`task_wdt: ... IDLE1 (CPU 1)` ve `Ringbuffer of AFE(FEED) is full`**
Komut tanıyıcı çekirdek 1'i dolduruyor. Artık yalnızca konuşma sırasında çalışıyor ve
`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n` ile bu çekirdekteki boşta görev denetimi
kapatıldı (Espressif'in konuşma örnekleri de böyle yapıyor). Uyarı konuşma sırasında yine
görünürse tanıma hâlâ gerçek zamanın gerisindedir; kalıcı çözüm WakeNet aşamasıdır.

**`realtime: API hatasi: seri log (... param=... msg=...)` ve ardından `setup failed`**
Servis, `session.update` içindeki bir alanı kabul etmiyor. Tek bir alan tüm yapılandırmayı
reddettirdiği için kurulum artık kademeli deniyor: önce her şey, sonra araçsız, sonra akıl
yürütme alanı olmadan, en sonda transkripsiyon olmadan. Logdaki `param=` hangi alanın
reddedildiğini söyler; `session configured without optional fields (level N)` satırı da hangi
kademede bağlanıldığını gösterir. Kalıcı çözüm için o alanı `menuconfig`'den kapat.

**Uyandırma tetiklenmiyor**
Seri logda beş saniyede bir mikrofon tepe seviyesi basılır:

```
speech: Microphone peak level over 5 s: 34/100
```

- `0/100` ise mikrofon veri üretmiyor: kodek, I2S slot ve kazancı kontrol et
  (`menuconfig > Ceko > Use right I2S microphone slot`). Bu haldeyken hiçbir uyandırma
  sözcüğü eşleşemez.
- Seviye geliyorsa ama uyanmıyorsa `Wake word detected` satırı hiç çıkmıyordur; eşiği
  düşür (`menuconfig > Ceko > WakeNet detection threshold`, örneğin 60) ve sözcüğü
  ekrandan uzaklaşmadan, normal ses tonuyla dene.
- Açılışta `No WakeNet model in the 'model' partition` satırı varsa model seçili değildir:
  `menuconfig > ESP Speech Recognition > Load Multiple Wake Words`.

Bu sırada uçtan uca akışı ekrana dokunarak veya BOOT düğmesiyle deneyebilirsin.

**`Hedef yonga 'esp32'`** Repoya daha önce `CONFIG_IDF_TARGET="esp32"` içeren bir
`sdkconfig` girmişti ve derlemeyi yanlış yongaya yönlendiriyordu. Dosya artık
izlenmiyor; yukarıdaki `set-target` komutu doğrusunu üretir.

`dependencies.lock` bu repoya `esp32` hedefiyle ve eski `esp_codec_dev` sürümüyle
çözülmüş halde girmişti. Yukarıdaki adımlardan sonra yeniden üretilir; oluşan yeni
dosyayı commit edebilirsin.

Başka bir managed bileşen de aynı `driver/...` hatasını verirse, adını kök
`CMakeLists.txt` içindeki köprü listesine ekle.

### Hoparlör

ES8311 mikrofon/ses devresi kartta bulunur. Kartın kendi hoparlör konektörüne uygun hoparlör
bağlı olmalı. Paketinde hoparlör yoksa uygun parça gerekir; konektör ve yük empedansı kartın
şeması/ürün revizyonundan kontrol edilmelidir. GPIO'ya doğrudan hoparlör bağlanmaz.

### menuconfig'de yaptığın ayarlar

`menuconfig` değişiklikleri yalnızca `sdkconfig` dosyasına yazılır; o dosya repoda izlenmiyor
ve `idf.py set-target` ile silinir. **Kalıcı olması gereken her ayar `sdkconfig.defaults`
içine de yazılmalı.** Aksi halde ayar bir sonraki temiz kurulumda sessizce kaybolur.

### API anahtarının yeri

Bu kişisel prototipte anahtar `menuconfig` üzerinden firmware'e gömülür; flash'tan çıkarılabilir.
Bu yüzden firmware ve gerçek `sdkconfig` dosyanı paylaşma. `sdkconfig` artık `.gitignore`
içinde ve repoda izlenmiyor; `sdkconfig.defaults` üzerinden yeniden üretilir. Daha önce
repoya girmiş olan kopya `esp32` hedefiyle üretilmişti ve derlemeyi yanlış yongaya
yönlendiriyordu; bilgisayarındaki eski dosyayı silip `idf.py set-target esp32s3` çalıştır.
Ürünleşmede cihaz kimlik doğrulaması ve kısa ömürlü token veren backend eklenmeli.
TLS sertifika kontrolü açıktır, sistem saati NTP ile ayarlanır. Sertifika kontrolünü kapatma.

## Ayarlar

| Ayar | Varsayılan | Amaç |
|---|---|---|
| Servis | OpenAI Realtime | Gemini Live ile değiştirilebilir |
| Model | `gpt-realtime-2.1` | Mini'ye göre daha iyi cevap, ~3 kat ses maliyeti |
| Akıl yürütme | `low` | Gecikmeyi düşük tutar; boş bırakılırsa alan gönderilmez |
| Web araması | Açık | Gemini'de `googleSearch`; OpenAI'de MCP sunucusu (varsayılan Exa) |
| Bağlam taşıma | 4 tur | Kopma/yenileme sonrası taşınan tur sayısı; 0 kapatır |
| Uyandırma sözcüğü | `Hi ESP` (WakeNet9 `wn9_hiesp`) | ESP Speech Recognition menüsünden seçilir |
| Uyandırma eşiği | 0 (model varsayılanı) | 40-99 ile elle bastırılabilir |
| Dokunmatik | Açık | Ekrana dokununca dinlemeye geçer |
| BOOT düğmesi | Açık | Beklerken basınca dinlemeye geçer |
| Takip penceresi | 6 saniye | Cevaptan sonra uyandırma sözcüğü gerekmez; 0 kapatır |
| Güven eşiği | %85 | Yanlış uyanma/kaçırma dengesi |
| Sessizlik | 1000 ms | Sorunun bittiğine karar verme |
| Maksimum kayıt | 20 saniye | RAM ve kullanım sınırı; aşılırsa kayıt gönderilmez |
| Konuşma bekleme | 5 saniye | Uyandırmadan sonra ses yoksa ücretsiz beklemeye döner |
| Yanıt sınırı | 400 token / 60 sn ses | Kısa cevap ve sınırlı kuyruk |
| Mikrofon I2S slot | Sol | Ses gelmezse donanım testinde sağ slot denenebilir |
| Ekran yönü | 180° | Üretici demo yönü; menüden değiştirilebilir |

Oturum açılışta kurulur ve açık tutulur; ilk yanıt gecikmesine artık TLS/oturum açma
süresi eklenmez. Servisin oturum ömrü dolmadan, cihaz **boştayken** yeni oturuma geçilir;
bağlantı koparsa 2 saniyeden 60 saniyeye çıkan aralıklarla arka planda yeniden denenir.
Oturumun açık olması sesin gönderildiği anlamına gelmez: beklemedeki ses, uyandırma
çerçevesi ve cevap sırasındaki mikrofon sesi gönderilmez; sunucu tarafı ses algılama
kapalıdır. Sohbet hafızası ücretsiz değildir; her tur önceki konuşmayı da girdi sayar,
`Conversation turns carried across a reconnect` ile sınırlanabilir.
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
| `main/speech.c` | Yerel AFE, WakeNet uyandırma, BOOT düğmesi ve kayıt |
| `main/touch.c` | Dokunmatik panel; dokununca dinlemeye geçer |
| `main/realtime.c` | Kalıcı WebSocket oturumu, ses kuyruğu, hata ve zaman aşımı |
| `main/rt_openai.c` | OpenAI Realtime protokolü, araçlar, transkript |
| `main/rt_gemini.c` | Gemini Live protokolü, Google araması, oturum devamı |
| `main/session_policy.c` | Bağlan/yenile/geri çekil kararları |
| `main/history.c` | Yeniden bağlanınca taşınan kısa konuşma özeti |
| `main/capture_gate.c` | Konuşma/sessizlik ve kayıt süresi sınırları |
| `main/audio_math.c` | 16↔24 kHz FIR dönüşümü ve RMS |
| `main/ws_message.c` | Sınırlı boyutlu parçalı WebSocket mesaj birleştirme |
| `tests/test_core.c` | Donanımdan bağımsız sınır/akış testleri |
| `docs/VALIDATION.md` | Gerçekte yapılan kontroller ve kalan fiziksel testler |
| `docs/PROVIDERS.md` | Servis karşılaştırması, fiyatlar ve mimari kararlar |

## Test

C derleyicisi bulunan bilgisayarda:

```sh
bash tools/test.sh
```

AddressSanitizer/UndefinedBehaviorSanitizer ile kayıt sınırları, parçalı mesajlar, ses
akışı, konuşma özeti ve oturum yenileme/geri çekilme kuralları doğrulanır. Aynı komut,
ESP-IDF olmayan makinede `tools/syntax-check.sh` ile taşıma ve protokol dosyalarını da
tip kontrolünden geçirir; bu `idf.py build` yerine geçmez. Ptrace kullanan ortamlarda LeakSanitizer çalışmıyorsa:
`ASAN_OPTIONS=detect_leaks=0 bash tools/test.sh` (sadece leak kontrolü kapanır).

## Kaynaklar

- [Waveshare kart örnekleri](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.32)
  — referans commit `b3d6546e39db6287bc643eb0b0ad796e0ff61315`.
- [ESP-SR komut tanıma](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html).
- [OpenAI Realtime konuşma olayları](https://developers.openai.com/api/docs/guides/realtime-conversations).
- [OpenAI WebSocket bağlantısı](https://developers.openai.com/api/docs/guides/voice-websockets).
- [GPT-Realtime-2.1](https://developers.openai.com/api/docs/models/gpt-realtime-2.1).
- [Gemini Live API WebSocket referansı](https://ai.google.dev/api/live).

Kart pinleri ve panel init değerleri üretici referansından doğrulandı. Uygulama kodu bu
prototip için yazıldı. ESP-IDF, ESP-SR, LVGL ve codec bileşenleri kendi lisansları altındadır;
model dosyaları otomatik indirilen Espressif bileşeninin parçasıdır.
