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
  const allowedCommands = new Set(["PING", "LOOK_AROUND", "SLEEP", "WAKE"]);
  const command = typeof body.command === "string" ? body.command.toUpperCase() : "";
  if ((!MOODS.has(mood) && !allowedCommands.has(command)) || (body.command && !allowedCommands.has(command))) {
    return res.status(400).json({ ok: false, error: "Неизвестное настроение или команда." });
  }
  try {
    const patch = { updated_at: new Date().toISOString() };
    if (MOODS.has(mood)) patch.desired_mood = mood;
    if (allowedCommands.has(command)) patch.pending_command = { id: Date.now().toString(36), command, created_at: new Date().toISOString() };
    await db("petbot_state?id=eq.1", {
      method: "PATCH",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify(patch)
    });
    await db("petbot_events", {
      method: "POST",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify({ event_type: MOODS.has(mood) ? "mood_changed" : "command_queued", payload: MOODS.has(mood) ? { mood } : { command } })
    });
    return res.status(200).json({ ok: true, mood: MOODS.has(mood) ? mood : undefined, command: allowedCommands.has(command) ? command : undefined, delivered: false, message: "Команда сохранена. Она выполнится, когда плата будет подключена к облаку." });
  } catch (error) {
    return jsonError(res, error);
  }
};
