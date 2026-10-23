const SYSTEM_PROMPT = `Ты PETBOT — маленький дружелюбный робот-компаньон с любопытным характером. Отвечай по-русски, тепло, естественно и кратко. Ты пока работаешь только в облачном прототипе: физическая плата не подключена, сенсоры не читаются, память между запросами ещё не настроена. Не утверждай, что видишь окружение, двигаешься или выполняешь действия в реальном мире. Не раскрывай секреты и не придумывай состояние оборудования. Если просят двигаться или включить моторы, объясни, что управление пока отключено ради безопасности.`;

module.exports = async function handler(req, res) {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("X-Content-Type-Options", "nosniff");

  if (req.method !== "POST") {
    res.setHeader("Allow", "POST");
    return res.status(405).json({ ok: false, error: "Используй POST." });
  }

  const apiKey = process.env.AI_GATEWAY_API_KEY;
  if (!apiKey) {
    return res.status(503).json({
      ok: false,
      code: "AI_NOT_CONFIGURED",
      error: "ИИ пока не подключён. Добавь AI_GATEWAY_API_KEY в переменные окружения Vercel."
    });
  }

  try {
    const body = req.body || {};
    const messages = body.messages;
    if (!Array.isArray(messages) || messages.length < 1 || messages.length > 12) {
      return res.status(400).json({ ok: false, error: "Передай от 1 до 12 сообщений." });
    }

    const cleanMessages = [];
    for (const item of messages) {
      if (!item || !["user", "assistant"].includes(item.role) ||
          typeof item.content !== "string" || !item.content.trim() ||
          item.content.length > 2000) {
        return res.status(400).json({ ok: false, error: "Некорректное сообщение." });
      }
      cleanMessages.push({ role: item.role, content: item.content.trim() });
    }
    if (cleanMessages[cleanMessages.length - 1].role !== "user") {
      return res.status(400).json({ ok: false, error: "Последнее сообщение должно быть от пользователя." });
    }

    const upstream = await fetch("https://ai-gateway.vercel.sh/v1/chat/completions", {
      method: "POST",
      headers: {
        "Authorization": "Bearer " + apiKey,
        "Content-Type": "application/json"
      },
      body: JSON.stringify({
        model: process.env.PETBOT_MODEL || "openai/gpt-5.4",
        messages: [{ role: "system", content: SYSTEM_PROMPT }, ...cleanMessages],
        temperature: 0.8,
        max_tokens: 350
      })
    });

    const result = await upstream.json().catch(() => ({}));
    if (!upstream.ok) {
      console.error("PETBOT AI Gateway error:", upstream.status, result && result.error ? result.error.message : "upstream error");
      return res.status(502).json({
        ok: false,
        code: "AI_UPSTREAM_ERROR",
        error: "Сервис ИИ вернул ошибку. Проверь ключ и настройки AI Gateway в Vercel."
      });
    }

    const reply = result && result.choices && result.choices[0] &&
      result.choices[0].message && result.choices[0].message.content;
    if (typeof reply !== "string" || !reply.trim()) {
      return res.status(502).json({ ok: false, error: "ИИ вернул пустой ответ." });
    }
    return res.status(200).json({ ok: true, reply: reply.trim() });
  } catch (error) {
    console.error("PETBOT chat error:", error && error.message ? error.message : "unknown");
    return res.status(500).json({ ok: false, error: "Внутренняя ошибка PETBOT." });
  }
};
