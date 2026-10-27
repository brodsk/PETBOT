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
        model: "google/gemini-2.5-flash-lite",
        messages: [{ role: "user", content: [
          { type: "text", text: "Ты — тихий визуальный наблюдатель PETBOT. Составь краткое описание текущего состояния сцены, чтобы использовать его как контекст для будущего разговора, а не как отдельное сообщение пользователю. Укажи только заметные объекты, людей, действия и изменения обстановки, которые действительно видны. Не обращайся к пользователю, не задавай вопросов и не формулируй разговорную реплику. Не угадывай личность, имя, характер, эмоции, возраст или другие личные данные. Если сцена почти не отличается от обычной, всё равно дай короткое нейтральное описание. Ответ 1–2 предложения." },
          { type: "image_url", image_url: { url: image, detail: "high" } }
        ] }],
        temperature: 0.2,
        max_tokens: 250
      })
    });
    const result = await upstream.json().catch(() => ({}));
    if (!upstream.ok) {
      console.error("PETBOT vision gateway error:", upstream.status, result && result.error ? result.error.message : "upstream error");
      return res.status(502).json({ ok: false, code: "VISION_UPSTREAM_ERROR", upstreamStatus: upstream.status, error: "ИИ не смог обработать изображение (HTTP " + upstream.status + "). Проверь ключ AI Gateway и доступность vision-модели в Vercel." });
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