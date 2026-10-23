const MOODS = new Set(["HAPPY", "CURIOUS", "SLEEPY", "SURPRISED", "ANNOYED", "LOVE"]);

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
  const response = await fetch(cfg.url + "/rest/v1/" + path, {
    ...options,
    headers: {
      apikey: cfg.key,
      Authorization: "Bearer " + cfg.key,
      "Content-Type": "application/json",
      Accept: "application/json",
      ...(options.headers || {})
    }
  });
  if (!response.ok) {
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

module.exports = { MOODS, db, authorizedDevice, jsonError };
