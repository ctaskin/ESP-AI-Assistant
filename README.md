# Ceko v0.2 — ESP-IDF sesli masaüstü asistanı

**Kart:** Waveshare ESP32-S3-Touch-AMOLED-1.32, 466×466, 8 MB Flash / 8 MB PSRAM, ES8311.
**Hedef:** ESP-IDF 5.5.2; Arduino kullanılmaz.

## Davranış

1. Siyah ekranda turkuaz iki göz aralıklarla kırpılır, hafifçe etrafa bakar.
2. Beklerken ses yalnızca cihazdaki AFE ve deneysel komut tanıyıcıda işlenir; internete gönderilmez.
3. “Hey ceko” algılanınca gözler büyür ve yeşile döner. `Dinliyorum` görünür.
4. Bundan sonraki konuşma alınır; 1 saniyelik sessizlikte kayıt tamamlanır.
5. Ses, **açılışta kurulmuş ve açık tutulan** WebSocket oturumundan gönderilir.
   Yanıt geldikçe hoparlörden çalınır.
6. Ağız, **çalınan sesin RMS şiddetine** göre açılıp kapanır. Fonem/dudak eşleştirmesi değildir.
7. Cevap bitince tekrar “hey ceko” bekler. **Sohbet aynı oturumda devam eder:** az önce
   konuşulan konuya atıf yapabilirsin. Bağlantı koparsa veya oturum yenilenirse son
   turların kısa özeti yeni oturuma taşınır.

**Bas-konuş yok.** Turun bittiğini yerel sessizlik algılama bildirir.
Bilgisayar/bridge açık kalmadan, kart Wi-Fi üzerinden doğrudan servise bağlanır.

**Servis seçimi:** `menuconfig > Ceko > Realtime voice service` ile OpenAI Realtime
veya Google Gemini Live seçilir. Güncel bilgi gerektiren sorular için Gemini'de
Google araması servis tarafında çalışır; OpenAI'de uzak bir MCP arama sunucusu
adresi girmek gerekir. Karşılaştırma, fiyatlar ve gerekçe: `docs/PROVIDERS.md`.

## Önce bilmen gereken iki sınır

- **“Hey ceko” deneysel:** Hazır/eğitilmiş Türkçe WakeNet modeli yok. MultiNet7 İngilizce
  komut tanıyıcısı, `hey jeko` yazımıyla sürekli çalıştırılıyor ve zaman aşımlarında sıfırlanıyor.
  Bu, Espressif'in önerdiği WakeNet → MultiNet zincirinin yerine kullanılan prototip yaklaşımıdır.
  Türkçe /ceko/ telaffuzunun tanınması ve yanlış uyanma oranı fiziksel kartta doğrulanmadı.
  `menuconfig > Ceko` altında yazım ve güven eşiği değiştirilebilir. Güvenilir ürün için özel
  wake-word modeli eğitimi/entegrasyonu gerekir. Alternatif ifade sessizce devreye sokulmaz.
- **Yarı çift yönlü:** Ceko cevap verirken mikrofon işlenip atılır. Kendi sesine uyanmaz;
  fakat konuşurken sözünü kesme yoktur. Bu sürümde akustik yankı giderme kapalıdır.
- **Gemini yolu doğrulanmadı:** Gemini Live mesaj alanları dokümantasyondan yazıldı,
  canlı bir hesapla denenmedi. OpenAI yolu da bu projede canlı çağrıyla test edilmedi.

Uyandırma tanıyıcısı gecikmeli karar verebildiği için başlangıçta “hey ceko” dedikten sonra
**gözler yeşile dönünce konuş**. Aynı nefeste devam edilen komutun ilk hecesi kaçabilir.

## Kurulum — Mac / VS Code

1. ZIP'i aç, `ceko` klasörünü VS Code ile aç.
2. ESP-IDF eklentisinde **5.5.2** kurulumunu seç. `ESP-IDF: Open ESP-IDF Terminal` aç.
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

**ESP-IDF sürümü 5.5.x olmalı.** IDF 6.x ile derleme `esp_codec_dev` içinde
`driver/gpio.h bulunamadı` hatasıyla durur; `CMakeLists.txt` bunu en başta açık bir mesajla
engeller. Ayrıntı ve kurtarma adımları aşağıdaki "Derleme sorunları" bölümünde.

`XXXX` yerine eklentinin bulduğu gerçek portu yaz. İlk yüklemede **tam `flash`** yap:
MultiNet model bölümü de yüklenir. Yalnızca `app-flash` kullanma. Seri monitörden çıkış: `Ctrl+]`.
Kart görünmüyorsa BOOT'a basılı tutarak USB'ye bağla, ardından bırak.

Kullanılabilir OpenAI model adları hesap bazında değişebilir. `model_not_found`, `invalid_api_key`
veya kota hatası seri logda kod olarak görünür. Burada gerçek API hesabıyla doğrulama yapılmadı.

### Derleme sorunları

**`fatal error: driver/gpio.h: No such file or directory` (esp_codec_dev içinde)**
ESP-IDF 6.x kullanıyorsun. IDF 6.0'da eski `driver` bileşeni `esp_driver_gpio` /
`esp_driver_i2c` başlıklarını artık dışarı açmıyor; `esp_codec_dev` bu başlıkları
bulamıyor. Bu proje ESP-IDF **5.5.x** ile derlenir (`main/idf_component.yml`:
`>=5.5.0,<6.0.0`).

**`Hedef yonga 'esp32'`** Proje yalnızca ESP32-S3 içindir. Eski `sdkconfig` dosyası
hedefi `esp32` olarak sabitliyordu.

Temiz başlangıç (ESP-IDF 5.5.x terminalinde):

```sh
idf.py --version                       # v5.5.x görmelisin
rm -rf build managed_components dependencies.lock sdkconfig
idf.py set-target esp32s3
idf.py build
```

`dependencies.lock` bu repoya IDF 6.1 ve `esp32` hedefiyle çözülmüş halde girmişti.
Yukarıdaki adımlardan sonra doğru sürümlerle yeniden üretilir; oluşan yeni dosyayı
commit edebilirsin.

### Hoparlör

ES8311 mikrofon/ses devresi kartta bulunur. Kartın kendi hoparlör konektörüne uygun hoparlör
bağlı olmalı. Paketinde hoparlör yoksa uygun parça gerekir; konektör ve yük empedansı kartın
şeması/ürün revizyonundan kontrol edilmelidir. GPIO'ya doğrudan hoparlör bağlanmaz.

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
| Web araması | Açık | Gemini'de Google araması; OpenAI'de MCP sunucusu adresi ister |
| Bağlam taşıma | 4 tur | Kopma/yenileme sonrası taşınan tur sayısı; 0 kapatır |
| Uyandırma yazımı | `hey jeko` | Türkçe ceko için deneysel İngilizce yazım |
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
| `main/speech.c` | Yerel AFE, MultiNet, uyandırma ve kayıt |
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
