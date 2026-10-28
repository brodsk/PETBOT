const { MOODS, db, jsonError } = require("../_petbot");

module.exports = async function handler(req, res) {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("X-Content-Type-Options", "nosniff");
  if (req.method !== "POST") {
    res.setHeader("Allow", "POST");
    return res.status(405).json({ ok: false, error: "Используй POST." });
  }
  const body = req.body || {};
  const mood = typeof body.mood === "string" ? body.mood.toUpperCase() : "";
  const allowedCommands = new Set(["PING", "LOOK_AROUND", "SLEEP", "WAKE", "SOUND_ON", "SOUND_OFF"]);
  const command = typeof body.command === "string" ? body.command.toUpperCase() : "";
  const rawMessage = typeof body.message === "string" ? body.message : "";
  const notification = rawMessage.trim().replace(/[\r\n\t]/g, " ").replace(/["\\]/g, "").replace(/[^\x20-\x7E]/g, "").replace(/\s+/g, " ").slice(0, 80).trim();
  const isNotification = Boolean(notification);
  if (isNotification && (body.mood || body.command)) {
    return res.status(400).json({ ok: false, error: "Отправляй сообщение отдельно от других команд." });
  }
  if ((!MOODS.has(mood) && !allowedCommands.has(command) && !isNotification) || (body.command && !allowedCommands.has(command))) {
    return res.status(400).json({ ok: false, error: "Неизвестное настроение или команда." });
  }
  if (rawMessage && !isNotification) {
    return res.status(400).json({ ok: false, error: "Сообщение должно содержать латинские буквы или цифры." });
  }
  try {
    const patch = { updated_at: new Date().toISOString() };
    if (MOODS.has(mood)) patch.desired_mood = mood;
    if (allowedCommands.has(command)) patch.pending_command = { id: Date.now().toString(36), command, created_at: new Date().toISOString() };
    if (isNotification) patch.pending_command = { id: Date.now().toString(36), command: "MESSAGE", message: notification, created_at: new Date().toISOString() };
    await db("petbot_state?id=eq.1", {
      method: "PATCH",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify(patch)
    });

    // Event history is secondary: a logging failure must not report the
    // whole command as failed after the desired state was already saved.
    try {
      await db("petbot_events", {
        method: "POST",
        headers: { Prefer: "return=minimal" },
        body: JSON.stringify({
          event_type: MOODS.has(mood) ? "mood_changed" : isNotification ? "notification_queued" : "command_queued",
          payload: MOODS.has(mood) ? { mood } : isNotification ? { message: notification } : { command }
        })
      });
    } catch (eventError) {
      console.error("PETBOT event logging failed; state update succeeded", {
        status: eventError.statusCode || 500
      });
    }

    return res.status(200).json({
      ok: true,
      mood: MOODS.has(mood) ? mood : undefined,
      command: isNotification ? "MESSAGE" : allowedCommands.has(command) ? command : undefined,
      delivered: false,
      message: isNotification ? "Сообщение поставлено в очередь. Оно появится на OLED, когда плата опросит сервер." : "Команда сохранена. Она выполнится, когда плата будет подключена к облаку."
    });
  } catch (error) {
    return jsonError(res, error);
  }
};
