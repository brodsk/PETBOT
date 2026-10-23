const { db } = require("./_petbot");

export default async function handler(_request, response) {
  response.setHeader("Cache-Control", "no-store");
  response.setHeader("X-Content-Type-Options", "nosniff");
  const result = {
    ok: true,
    service: "petbot-cloud",
    version: "0.2.0",
    robotConnected: false,
    motorControlEnabled: false,
    cloudControlConfigured: Boolean(process.env.SUPABASE_URL || process.env.NEXT_PUBLIC_SUPABASE_URL) && Boolean(process.env.SUPABASE_SERVICE_ROLE_KEY) && Boolean(process.env.PETBOT_DEVICE_TOKEN),
    timestamp: new Date().toISOString()
  };
  try {
    const rows = await db("petbot_state?select=desired_mood,device_online,device_last_seen,device_info&id=eq.1");
    const state = rows && rows[0];
    if (state) {
      const lastSeen = state.device_last_seen ? new Date(state.device_last_seen).getTime() : 0;
      result.robotConnected = Boolean(state.device_online && Date.now() - lastSeen < 30000);
      result.mood = state.desired_mood;
      result.lastSeen = state.device_last_seen;
      result.deviceInfo = state.device_info || {};
    }
  } catch (_) {
    // Keep the health check available even before cloud storage is configured.
  }
  response.status(200).json(result);
}
