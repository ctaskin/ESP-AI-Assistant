# Ceko v0.2 — servis seçimi ve gecikme/hafıza/internet kararları

Bu belge, "model yavaş, internete çıkamıyor, bağlamı unutuyor" şikâyetlerinin
hangisinin model seçimiyle, hangisinin kodun mimarisiyle ilgili olduğunu ve
neyin değiştirildiğini kaydeder. Fiyat ve protokol bilgileri **Eylül 2026**
tarihli kamuya açık dokümantasyondan alındı; hesabında geçerli olan değerleri
ilk canlı testte doğrula.

## 1. Sorunların gerçek kaynağı

| Şikâyet | Gerçek neden | v0.2'de yapılan |
|---|---|---|
| Yavaş | Her soru için yeni TLS + WebSocket + oturum kurulumu | Oturum açılışta kurulur, açık tutulur, boştayken yenilenir |
| Bağlamı unutuyor | Her soru ayrı oturumdu | Tek oturum sürdürülür; kopma/yenileme olursa son turlar özet olarak taşınır |
| İnternete çıkamıyor | Modele arama aracı verilmemişti | Gemini'de `googleSearch`, OpenAI'de uzak MCP arama sunucusu (veya deneysel hosted web search) |
| Cevap kalitesi | Mini modelin kapasitesi | Varsayılan `gpt-realtime-2.1`; `reasoning.effort` ayarlanabilir |

Yalnızca model adını değiştirmek ilk üç maddeyi çözmez. Asıl kazanç kalıcı
oturumdadır: soru bittiğinde TLS el sıkışması ve `session.update` turu artık
ilk yanıt gecikmesine eklenmiyor.

## 2. Servis karşılaştırması

| | gpt-realtime-2.1 | gpt-realtime-2.1-mini | gemini-3.1-flash-live-preview |
|---|---|---|---|
| Ses girişi | $32 / 1M token | $10 / 1M | $3 / 1M |
| Ses çıkışı | $64 / 1M token | $20 / 1M | $12 / 1M |
| Token/saniye (ses) | ~10 giriş / ~20 çıkış | aynı | 32 giriş / 25 çıkış |
| Web araması | Yerleşik araç Realtime'da GA belgelenmiş değil; uzak MCP sunucusu veya kendi fonksiyonun gerekir | aynı | `tools:[{googleSearch:{}}]` — sunucu tarafında, ek altyapı yok |
| Oturum ömrü | 60 dakika (Azure'da 30) | aynı | Ses-only 15 dk; bağlam sıkıştırmasıyla sınırsız, bağlantı ~10 dk'da yenilenir |
| Bağlam taşıma | Sunucu tarafı devam yok; yeniden bağlanınca özet göndermek gerekir | aynı | `sessionResumption` handle'ı ile sunucuda devam |
| Cihaz tarafı ses | 24 kHz'e çevirmek gerekiyor | aynı | 16 kHz doğrudan kabul ediliyor; ESP32'de yeniden örnekleme yok |
| Akıl yürütme ayarı | `reasoning.effort` (minimal…xhigh, varsayılan low) | var | ayrı parametre yok |
| Olgunluk | GA | GA | preview |

Kaba karşılaştırma: 6 saniyelik soru + 10 saniyelik cevap, bağlam tekrarı hariç,
`gpt-realtime-2.1` ile yaklaşık 1,5 cent; mini ile yaklaşık 0,5 cent; Gemini
Live ile yaklaşık 0,4 cent. Kalıcı oturum ve hafıza, her turda önceki konuşmanın
tekrar girdi olarak sayılması demektir; önbelleğe alınan girdi daha ucuz
fiyatlanır ama bedava değildir. Hafızayı `menuconfig` üzerinden kısabilirsin.

## 3. Öneri

- **Ceko'nun internete çıkması senin için birinci öncelikse Gemini Live mantıklı.**
  Google araması servis tarafında çalışır; karta ikinci bir TLS bağlantısı, ikinci
  bir anahtar veya ev içinde açık duran bir backend gerekmez. Üstelik ses çıkışı
  OpenAI mini'nin bile üçte biri fiyatında ve giriş sesi 16 kHz kabul edildiği için
  ESP32 bir dönüşüm işinden kurtulur.
- **OpenAI'de kalmak istersen** varsayılan artık `gpt-realtime-2.1`. Arama için
  ya uzak bir MCP arama sunucusu adresi girmen ya da deneysel hosted `web_search`
  aracının hesabında çalışmasını ummanız gerekir. Gemini tarafındaki `googleSearch`
  kadar kesin bir yol değildir.
- Türkçe ses kalitesi ve gerçek gecikme **iki serviste de bu projede ölçülmedi.**
  Karar vermeden önce ikisini de aynı sorularla dene: `menuconfig > Ceko > Realtime
  voice service` seçeneği tek değişiklikle geçiş yapar.

## 4. Uygulanan mimari

- `main/realtime.c` artık taşıma katmanı: bağlantıyı kurar, açık tutar, `ping`
  gönderir, kopunca 2→60 saniye artan gecikmeyle yeniden dener ve oturum ömrü
  dolmadan **cihaz boştayken** yeniler. Uyandırma anında bağlantı zaten hazırdır.
- `main/rt_openai.c` ve `main/rt_gemini.c` yalnızca protokol farkını içerir;
  `menuconfig`'de seçilen dosya derlenir.
- `main/session_policy.c` bağlan/yenile/bekle kararlarını donanımdan bağımsız
  tutar ve bilgisayarda test edilir.
- `main/history.c` son turların metin özetini RAM'de tutar. Yalnızca bağlantı
  yenilendiğinde/koptuğunda yeni oturuma taşınır; flash'a yazılmaz, cihaz
  kapanınca kaybolur. Servisten gelen metinlerdeki kontrol karakterleri
  temizlenir, böylece bir transkript sahte "Kullanıcı:" satırı üretemez.
- Gizlilik kuralı değişmedi: mikrofon sesi yalnızca uyandırma sonrası ve
  yerel sessizlik kararından sonra gönderilir. Oturumun açık durması, bekleme
  sesinin gönderildiği anlamına gelmez; Gemini'de sunucu VAD'ı kapalıdır,
  OpenAI'de `turn_detection` null bırakılmıştır.

## 5. Doğrulanmayanlar

- Hiçbir canlı API çağrısı yapılmadı; bu ortamda ESP-IDF ve internet erişimi yok.
- Gemini Live mesaj alanları (`setup`, `realtimeInput.audio`, `activityStart/End`,
  `serverContent`, `sessionResumptionUpdate`) dokümantasyondan yazıldı, canlı
  oturumla doğrulanmadı.
- OpenAI'de `reasoning.effort` ve `tools:[{type:"web_search"}]` alanlarının
  Realtime oturumunda kabul edilip edilmediği hesapta test edilmedi; ikisi de
  `menuconfig`'den kapatılabilir.

## Kaynaklar

- [GPT-Realtime-2.1 model sayfası](https://developers.openai.com/api/docs/models/gpt-realtime-2.1)
- [OpenAI API fiyatlandırma](https://developers.openai.com/api/docs/pricing)
- [Realtime conversations — bağlam ve kırpma](https://developers.openai.com/api/docs/guides/realtime-conversations)
- [Realtime with tools — MCP ve yerleşik araçlar](https://developers.openai.com/api/docs/guides/realtime-mcp)
- [Azure OpenAI Realtime — oturum süresi](https://learn.microsoft.com/en-us/azure/foundry/openai/how-to/realtime-audio)
- [Gemini Live API — oturum yönetimi, sıkıştırma, resumption](https://ai.google.dev/gemini-api/docs/live-session)
- [Gemini Live API — araçlar ve Google araması](https://ai.google.dev/gemini-api/docs/live-api/tools)
- [Gemini Live API — WebSocket referansı](https://ai.google.dev/api/live)
- [Gemini API fiyatlandırma](https://ai.google.dev/gemini-api/docs/pricing)
