module.exports = async function handler(req, res) {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("X-Content-Type-Options", "nosniff");
  if (req.method !== "POST") {
    res.setHeader("Allow", "POST");
    return res.status(405).json({ ok: false, error: "Используй POST." });
  }
  const apiKey = process.env.AI_GATEWAY_API_KEY;
  if (!apiKey) return res.status(503).json({ ok: false, code: "AI_NOT_CONFIGURED", error: "ИИ пока не подключён: проверь AI_GATEWAY_API_KEY в Vercel." });
  try {
    const image = req.body && req.body.image;
    if (typeof image !== "string" || image.length > 1800000 || !/^data:image\/(jpeg|png|webp);base64,[A-Za-z0-9+/=]+$/.test(image)) {
      return res.status(400).json({ ok: false, error: "Не удалось прочитать кадр. Перезапусти камеру и попробуй ещё раз." });
    }
    const upstream = await fetch("https://ai-gateway.vercel.sh/v1/chat/completions", {
      method: "POST",
      headers: { "Authorization": "Bearer " + apiKey, "Content-Type": "application/json" },
      body: JSON.stringify({
        model: "anthropic/claude-sonnet-4.5",
        messages: [{ role: "user", content: [
          { type: "text", text: "Ты анализируешь один кадр с камеры PETBOT по явному запросу пользователя. Кратко на русском опиши только то, что действительно видно: людей, предметы, обстановку и заметные действия. Не угадывай личность, имя, характер, эмоции, возраст или другие личные данные человека. Если что-то неразборчиво, прямо скажи. Ответ 1–4 предложения." },
          { type: "image_url", image_url: { url: image, detail: "low" } }
        ] }],
        temperature: 0.2,
        max_tokens: 250
      })
    });
    const result = await upstream.json().catch(() => ({}));
    if (!upstream.ok) {
      console.error("PETBOT vision gateway error:", upstream.status, result && result.error ? result.error.message : "upstream error");
      return res.status(502).json({ ok: false, code: "VISION_UPSTREAM_ERROR", upstreamStatus: upstream.status, error: "ИИ не смог обработать изображение. Попробуй ещё раз." });
    }
    const content = result && result.choices && result.choices[0] && result.choices[0].message && result.choices[0].message.content;
    const description = typeof content === "string" ? content.trim() : Array.isArray(content) ? content.map(part => part && typeof part.text === "string" ? part.text : "").join("").trim() : "";
    if (!description) return res.status(502).json({ ok: false, error: "ИИ не вернул описание кадра. Попробуй ещё раз." });
    return res.status(200).json({ ok: true, description });
  } catch (error) {
    console.error("PETBOT vision error:", error && error.message ? error.message : "unknown");
    return res.status(500).json({ ok: false, error: "Внутренняя ошибка анализа изображения." });
  }
};