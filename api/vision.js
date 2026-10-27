const { db } = require("./_petbot");

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
    let previousScene = "";
    try {
      const prior = await db("petbot_events?select=payload&event_type=eq.scene_observation&order=created_at.desc&limit=1");
      if (prior && prior[0] && prior[0].payload) previousScene = String(prior[0].payload.description || "").slice(0, 500);
    } catch (error) { console.error("PETBOT scene read failed:", error && error.message); }
    const upstream = await fetch("https://ai-gateway.vercel.sh/v1/chat/completions", {
      method: "POST",
      headers: { "Authorization": "Bearer " + apiKey, "Content-Type": "application/json" },
      body: JSON.stringify({
        model: "google/gemini-2.5-flash-lite",
        messages: [{ role: "user", content: [
          { type: "text", text: "Ты — тихий визуальный наблюдатель PETBOT. Предыдущее описание: " + (previousScene || "нет предыдущего наблюдения") + ". Верни только JSON-объект с полями description (короткое описание сцены), important_change (true/false), event (новое важное действие или пустая строка), importance (1–5). Важны: человек вошёл/вышел, объект появился/исчез, началось явно заметное действие, предмет перемещён, сцена существенно изменилась. Мелкие движения и одинаковые кадры не важны. Не угадывай личность, возраст или эмоции. Если не уверен, important_change=false. Не обращайся к пользователю." },
          { type: "image_url", image_url: { url: image, detail: "high" } }
        ] }],
        temperature: 0.1,
        max_tokens: 300
      })
    });
    const result = await upstream.json().catch(() => ({}));
    if (!upstream.ok) {
      console.error("PETBOT vision gateway error:", upstream.status, result && result.error ? result.error.message : "upstream error");
      if (upstream.status === 429) {
        res.setHeader("Retry-After", "90");
        return res.status(429).json({ ok: false, code: "VISION_RATE_LIMITED", error: "ИИ временно перегружен. Камера сделает паузу и продолжит позже." });
      }
      return res.status(502).json({ ok: false, code: "VISION_UPSTREAM_ERROR", upstreamStatus: upstream.status, error: "ИИ не смог обработать изображение (HTTP " + upstream.status + "). Проверь ключ AI Gateway и доступность vision-модели в Vercel." });
    }
    const content = result && result.choices && result.choices[0] && result.choices[0].message && result.choices[0].message.content;
    const raw = typeof content === "string" ? content.trim() : Array.isArray(content) ? content.map(part => part && typeof part.text === "string" ? part.text : "").join("").trim() : "";
    let parsed;
    try { parsed = JSON.parse(raw.replace(/^```(?:json)?\s*/i, "").replace(/\s*```$/, "")); }
    catch (_) { parsed = { description: raw, important_change: false, event: "", importance: 1 }; }
    const description = String(parsed.description || raw).trim().slice(0, 500);
    if (!description) return res.status(502).json({ ok: false, error: "ИИ не вернул описание кадра. Попробуй ещё раз." });
    const importantChange = parsed.important_change === true && Number(parsed.importance) >= 3 && String(parsed.event || "").trim().length > 0;
    try {
      await db("petbot_events", { method: "POST", headers: { Prefer: "return=minimal" }, body: JSON.stringify({ event_type: "scene_observation", payload: { description, important_change: importantChange, event: importantChange ? String(parsed.event).slice(0, 240) : "", importance: Math.max(1, Math.min(5, Number(parsed.importance) || 1)) } }) });
      if (importantChange) await db("petbot_events", { method: "POST", headers: { Prefer: "return=minimal" }, body: JSON.stringify({ event_type: "scene_important", payload: { description, event: String(parsed.event).slice(0, 240), importance: Number(parsed.importance) } }) });
    } catch (error) { console.error("PETBOT scene save failed:", error && error.message); }
    return res.status(200).json({ ok: true, description, important_change: importantChange, event: importantChange ? String(parsed.event).slice(0, 240) : "" });
  } catch (error) {
    console.error("PETBOT vision error:", error && error.message ? error.message : "unknown");
    return res.status(500).json({ ok: false, error: "Внутренняя ошибка анализа изображения." });
  }
};