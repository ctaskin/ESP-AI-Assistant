# Ceko v0.1 — ESP-IDF sesli masaüstü asistanı

**Kart:** Waveshare ESP32-S3-Touch-AMOLED-1.32, 466×466, 8 MB Flash / 8 MB PSRAM, ES8311.
**Hedef:** ESP-IDF 5.5.2; Arduino kullanılmaz. Repo oluşturulmadı.

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

## Önce bilmen gereken iki sınır

- **“Hey ceko” deneysel:** Hazır/eğitilmiş Türkçe WakeNet modeli yok. MultiNet7 İngilizce
  komut tanıyıcısı, `hey jeko` yazımıyla sürekli çalıştırılıyor ve zaman aşımlarında sıfırlanıyor.
  Bu, Espressif'in önerdiği WakeNet → MultiNet zincirinin yerine kullanılan prototip yaklaşımıdır.
  Türkçe /ceko/ telaffuzunun tanınması ve yanlış uyanma oranı fiziksel kartta doğrulanmadı.
  `menuconfig > Ceko` altında yazım ve güven eşiği değiştirilebilir. Güvenilir ürün için özel
  wake-word modeli eğitimi/entegrasyonu gerekir. Alternatif ifade sessizce devreye sokulmaz.
- **Yarı çift yönlü:** Ceko cevap verirken mikrofon işlenip atılır. Kendi sesine uyanmaz;
  fakat konuşurken sözünü kesme yoktur. Bu sürümde akustik yankı giderme kapalıdır.

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
   - `OpenAI API key`: ayrı API hesabının anahtarı. ChatGPT Plus dahil değildir.
   - `OpenAI Realtime model ID`: varsayılan `gpt-realtime-2.1-mini`.
   - Ses: `marin`, hoparlör seviyesi `%65`, mikrofon kazancı `24 dB`.
5. Kaydet/çık ve derle:

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

`XXXX` yerine eklentinin bulduğu gerçek portu yaz. İlk yüklemede **tam `flash`** yap:
MultiNet model bölümü de yüklenir. Yalnızca `app-flash` kullanma. Seri monitörden çıkış: `Ctrl+]`.
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

## Ayarlar

| Ayar | Varsayılan | Amaç |
|---|---|---|
| Uyandırma yazımı | `hey jeko` | Türkçe ceko için deneysel İngilizce yazım |
| Güven eşiği | %85 | Yanlış uyanma/kaçırma dengesi |
| Sessizlik | 1000 ms | Sorunun bittiğine karar verme |
| Maksimum kayıt | 20 saniye | RAM ve kullanım sınırı; aşılırsa kayıt gönderilmez |
| Konuşma bekleme | 5 saniye | Uyandırmadan sonra ses yoksa ücretsiz beklemeye döner |
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
| `main/speech.c` | Yerel AFE, MultiNet, uyandırma ve kayıt |
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
