const { db, authorizedDevice, jsonError } = require("../_petbot");

module.exports = async function handler(req, res) {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("X-Content-Type-Options", "nosniff");
  if (req.method !== "POST") {
    res.setHeader("Allow", "POST");
    return res.status(405).json({ ok: false, error: "Используй POST." });
  }
  if (!authorizedDevice(req)) return res.status(401).json({ ok: false, error: "Неверный токен устройства." });
  const body = req.body || {};
  const info = {
    firmware: typeof body.firmware === "string" ? body.firmware.slice(0, 40) : "unknown",
    wifi_rssi: Number.isFinite(body.wifi_rssi) ? Math.max(-120, Math.min(0, body.wifi_rssi)) : null,
    free_heap: Number.isFinite(body.free_heap) ? Math.max(0, body.free_heap) : null,
    ip: typeof body.ip === "string" ? body.ip.slice(0, 45) : null,
    mood: typeof body.mood === "string" && ["HAPPY", "CURIOUS", "SLEEPY", "SURPRISED", "ANNOYED", "LOVE", "THINKING", "BORED", "EXCITED"].includes(body.mood) ? body.mood : null,
    autonomy: body.autonomy === true,
    battery_mv: Number.isFinite(body.battery_mv) ? Math.max(0, Math.min(6000, body.battery_mv)) : null,
    uv_sensor_connected: body.uv_sensor_connected === true,
    camera_connected: body.camera_connected === true,
    motors_enabled: body.motors_enabled === true
  };
  try {
    await db("petbot_state?id=eq.1", {
      method: "PATCH",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify({ device_online: true, device_last_seen: new Date().toISOString(), device_info: info, updated_at: new Date().toISOString() })
    });
    await db("petbot_events", {
      method: "POST",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify({ event_type: "device_heartbeat", payload: info })
    });
    return res.status(200).json({ ok: true, server_time: new Date().toISOString(), poll_after_ms: 5000 });
  } catch (error) {
    return jsonError(res, error);
  }
};
