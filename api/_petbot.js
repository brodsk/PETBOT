const MOODS = new Set(["HAPPY", "CURIOUS", "SLEEPY", "SURPRISED", "ANNOYED", "LOVE", "SAD", "EXCITED", "SHY", "CONFUSED", "BORED", "SCARED"]);

const IMPULSES = new Set(["CHAT", "AFFECTION", "PRAISE", "SCOLD"]);
const DRIVE_KEYS = ["curiosity", "energy", "mood_value", "attachment", "stimulation", "social_need", "confidence"];

function cleanDrives(input) {
  if (!input || typeof input !== "object") return null;
  const out = {};
  for (const key of DRIVE_KEYS) {
    const v = input[key];
    if (!Number.isFinite(v)) return null;
    out[key] = Math.max(0, Math.min(100, Math.round(v)));
  }
  return out;
}

// Plain-language description of the robot's real internal state for the LLM prompt.
function describeDrives(d) {
  if (!d) return "";
  const lvl = (v, low, mid, high) => (v < 30 ? low : v < 65 ? mid : high);
  return [
    "энергия: " + lvl(d.energy, "почти нет сил, хочется спать", "нормальная", "много сил"),
    "настроение: " + lvl(d.mood_value, "грустноватое", "ровное", "приподнятое"),
    "любопытство: " + lvl(d.curiosity, "низкое", "умеренное", "высокое, всё интересно"),
    "потребность в общении: " + lvl(d.social_need, "наобщался", "обычная", "сильно соскучился по вниманию"),
    "привязанность к хозяину: " + lvl(d.attachment, "ещё привыкаешь", "тёплая", "очень сильная"),
    "возбуждение: " + lvl(d.stimulation, "спокойный", "бодрый", "взбудоражен"),
    "уверенность: " + lvl(d.confidence, "робеешь", "обычная", "уверен в себе")
  ].join("; ");
}

// The model is asked for JSON, but small models drift: accept fenced or bare JSON,
// and fall back to plain text so the user never sees raw JSON or an empty reply.
function parseModelReply(raw) {
  const text = String(raw || "").trim();
  const fallback = { reply: text.replace(/^```(?:json)?|```$/gim, "").trim(), mood: null, impulse: "CHAT" };
  const start = text.indexOf("{");
  const end = text.lastIndexOf("}");
  if (start < 0 || end <= start) return fallback;
  try {
    const obj = JSON.parse(text.slice(start, end + 1));
    const reply = typeof obj.reply === "string" ? obj.reply.trim() : "";
    if (!reply) return fallback;
    const mood = typeof obj.mood === "string" ? obj.mood.trim().toUpperCase() : "";
    const impulse = typeof obj.impulse === "string" ? obj.impulse.trim().toUpperCase() : "";
    return {
      reply: reply.slice(0, 1200),
      mood: MOODS.has(mood) ? mood : null,
      impulse: IMPULSES.has(impulse) ? impulse : "CHAT"
    };
  } catch (e) {
    return fallback;
  }
}

function config() {
  const url = process.env.SUPABASE_URL || process.env.NEXT_PUBLIC_SUPABASE_URL;
  const key = process.env.SUPABASE_SERVICE_ROLE_KEY;
  if (!url || !key) return null;
  return { url: url.replace(/\/$/, ""), key };
}

async function db(path, options = {}) {
  const cfg = config();
  if (!cfg) {
    const error = new Error("PETBOT cloud storage is not configured.");
    error.statusCode = 503;
    throw error;
  }
  const requestHeaders = {
    apikey: cfg.key,
    "Content-Type": "application/json",
    Accept: "application/json",
    ...(options.headers || {})
  };
  // Supabase's newer sb_secret_ keys are API keys, not JWTs; do not send them
  // as Bearer tokens. Legacy service_role JWT keys still need Authorization.
  if (!cfg.key.startsWith("sb_secret_")) {
    requestHeaders.Authorization = "Bearer " + cfg.key;
  }
  const response = await fetch(cfg.url + "/rest/v1/" + path, {
    ...options,
    headers: requestHeaders
  });
  if (!response.ok) {
    const detail = await response.text().catch(() => "");
    console.error("PETBOT Supabase request failed", {
      path,
      status: response.status,
      detail: detail.slice(0, 500)
    });
    const error = new Error("PETBOT cloud storage request failed.");
    error.statusCode = 502;
    throw error;
  }
  if (response.status === 204) return null;
  return response.json();
}

function authorizedDevice(req) {
  const expected = process.env.PETBOT_DEVICE_TOKEN;
  const auth = req.headers.authorization || "";
  return Boolean(expected && auth === "Bearer " + expected);
}

function jsonError(res, error) {
  const status = error.statusCode || 500;
  return res.status(status).json({
    ok: false,
    error: status === 503
      ? "Облачное хранилище PETBOT ещё не настроено."
      : status === 401
        ? "Неверный токен устройства."
        : "Ошибка облачного сервиса PETBOT."
  });
}

module.exports = { MOODS, IMPULSES, cleanDrives, describeDrives, parseModelReply, db, authorizedDevice, jsonError };
