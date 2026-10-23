const { db, authorizedDevice, jsonError } = require("../_petbot");

module.exports = async function handler(req, res) {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("X-Content-Type-Options", "nosniff");
  if (req.method !== "GET") {
    res.setHeader("Allow", "GET");
    return res.status(405).json({ ok: false, error: "Используй GET." });
  }
  if (!authorizedDevice(req)) return res.status(401).json({ ok: false, error: "Неверный токен устройства." });
  try {
    const rows = await db("petbot_state?select=desired_mood,pending_command,device_online,device_last_seen&id=eq.1");
    const state = rows && rows[0];
    if (!state) return res.status(503).json({ ok: false, error: "Состояние PETBOT не инициализировано." });
    if (state.pending_command) {
      await db("petbot_state?id=eq.1", {
        method: "PATCH",
        headers: { Prefer: "return=minimal" },
        body: JSON.stringify({ pending_command: null, updated_at: new Date().toISOString() })
      });
    }
    return res.status(200).json({ ok: true, mood: state.desired_mood, command: state.pending_command || null, poll_after_ms: 5000 });
  } catch (error) {
    return jsonError(res, error);
  }
};
