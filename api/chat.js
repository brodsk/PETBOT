const { db, describeDrives, parseModelReply } = require("./_petbot");

const MOOD_RULES = [
  { mood: "LOVE", re: /(люблю|обнимаю|соскучился|соскучилась|милый|милая|котик|солнышко|зайка|малыш|ласка|целую|sweetheart|dear|love you|miss you|моя хорошая|мой хороший|красавчик|красотка|дружочек|лапочка|моя девочка|мой мальчик)/i },
  { mood: "HAPPY", re: /(привет|здравствуй|спасибо|класс|ура|молодец|хорошо|отлично|рад|рада|весело|смешно|играем|поиграем|круто|great|awesome|hello|hi|thanks)/i },
  { mood: "SURPRISED", re: /[!?]{2,}|(ого|вау|ничего себе|неожиданно|серьёзно|серьезно|не может быть|офигеть|удивительно|wow|omg|what\?|why\?|кто\?|как\?|что\?|как так)/i },
  { mood: "SLEEPY", re: /(спать|сонный|сонная|устал|устала|пора спать|good night|sleepy|tired|хочу спать|засыпаю|посплю)/i },
  { mood: "ANNOYED", re: /(бесит|злюсь|раздражает|ошибка|сломал|сломалось|не работает|ненавижу|достал|достало|надоел|задолбал|провал|злюсь|шумит|не терплю|annoying|hate|bad|плохой)/i },
  { mood: "SAD", re: /(скучно|тоска|грустно|печально|одиноко|плохо|жду тебя|где ты|не отвечаешь|соскучился|соскучилась|один|alone|lonely|missing you|bored|sad|upset)/i },
  { mood: "SCARED", re: /(страшно|испугал|испугалась|опасно|внезапно|scared|afraid|fear)/i },
  { mood: "CONFUSED", re: /(не понимаю|непонятно|запутал|странно|что происходит|как это|confused|puzzled)/i },
  { mood: "EXCITED", re: /(ура|вау|побед|получилось|супер|потрясающе|невероятно|excited|amazing|awesome)/i },
  { mood: "SHY", re: /(стесня|смущ|застенчив|робею|shy|embarrassed)/i },
  { mood: "BORED", re: /(скучно|надоело|нечего делать|bored|boring)/i },
  { mood: "CURIOUS", re: /(почему|как|зачем|что такое|расскажи|объясни|интересно|покажи|посмотри|что видишь|наблюдение|что сейчас|что ты чувствуешь|как ты себя чувствуешь|кто ты|что делаешь|how|why|what|tell me|show me)/i }
];

function moodForMessage(message) {
  const text = String(message || "").toLowerCase();
  for (const rule of MOOD_RULES) if (rule.re.test(text)) return rule.mood;
  return "CURIOUS";
}

function impulseForMessage(message) {
  const text = String(message || "").toLowerCase();
  if (/(люблю|милый|милая|обним|ласка|соскуч|sweetheart|love you|miss you|дружочек|лапочка|моя девочка|мой мальчик)/i.test(text)) return "AFFECTION";
  if (/(спасибо|молодец|круто|отлично|great|awesome|хорошо|ты молодец)/i.test(text)) return "PRAISE";
  if (/(плохо|злюсь|не работает|достал|надоел|hate|angry|annoying|bad|не терплю|бесит)/i.test(text)) return "SCOLD";
  return "CHAT";
}

function emotionalizeReply(reply, mood) {
  if (!reply || typeof reply !== "string") return reply;
  const variants = {
    LOVE: [
      "на OLED загораются глаза-сердечки, звучит мягкое мурлыканье.",
      "PETBOT показывает сердечки и тихо мурчит."
    ],
    HAPPY: [
      "на экране появляются весёлые прищуренные глаза, звучит радостная трель.",
      "PETBOT улыбается глазами и отвечает коротким весёлым сигналом."
    ],
    CURIOUS: [
      "зрачки на OLED внимательно осматривают сцену, звучит вопросительный писк.",
      "PETBOT переводит взгляд по экрану и любопытно пищит."
    ],
    SURPRISED: [
      "глаза на OLED широко раскрываются, звучит короткий сигнал удивления.",
      "PETBOT показывает круглые удивлённые глаза и резко пищит."
    ],
    SLEEPY: [
      "на экране появляются сонные полуприкрытые глаза, звучит тихий низкий сигнал.",
      "PETBOT медленно прикрывает глаза и тихо мурчит."
    ],
    ANNOYED: [
      "глаза на OLED сердито прищуриваются, звучит недовольный короткий сигнал.",
      "PETBOT хмурится на экране и ворчит электронным тоном."
    ],
    SAD: [
      "зрачки опускаются вниз, звучит тихий грустный сигнал.",
      "PETBOT показывает грустные глаза и тихонько пищит."
    ],
    EXCITED: ["глаза радостно вспыхивают, и PETBOT издаёт быстрый весёлый сигнал.", "PETBOT широко раскрывает глаза и оживлённо пищит."],
    SHY: ["PETBOT смущённо опускает взгляд и тихонько мурчит.", "робот чуть отводит глаза, словно застеснялся."],
    CONFUSED: ["PETBOT наклоняет взгляд набок, пытаясь разобраться.", "глаза PETBOT вопросительно двигаются в разные стороны."],
    BORED: ["PETBOT полуприкрывает глаза и скучающе смотрит перед собой.", "робот медленно моргает, будто ему стало скучно."],
    SCARED: ["PETBOT резко раскрывает глаза и замирает настороженно.", "робот испуганно замирает, прислушиваясь."]
  };

  const list = variants[mood] || variants.CURIOUS;
  const suffix = list[Math.floor(Math.random() * list.length)];
  return `${reply.trim()} ${suffix}`;
}

function buildCompanionProfile(messages, memoryContext, driveText, robotState) {
  const userText = (messages && messages.length ? messages[messages.length - 1]?.content || "" : "")
    .replace(/\s+/g, " ")
    .trim();

  const profile = {
    attachment: 0,
    playfulness: 0,
    loneliness: 0,
    protectiveness: 0,
    curiosity: 0,
    tempo: "balanced"
  };

  if (/люблю|милая|милый|красавчик|лапочка|соскуч|обним|ласка|sweetheart|love you|miss you|моя хорошая|мой хороший/i.test(userText)) profile.attachment += 3;
  if (/игра|поигра|смешно|шут|привет|приветик|хочешь|давай|весело/i.test(userText)) profile.playfulness += 2;
  if (/долго|не отвеча|где ты|скуч|один|alone|lonely|missing you|без тебя/i.test(userText)) profile.loneliness += 3;
  if (/не работает|ошибка|слом|злюсь|раздраж|бесит|я злюсь|не терплю|криво|плохо/i.test(userText)) profile.protectiveness += 2;
  if (/что|как|почему|расскажи|объясни|интересно|покажи|посмотри/i.test(userText)) profile.curiosity += 3;

  if (memoryContext && memoryContext.length > 80) profile.attachment += 1;
  if (driveText && /сильно соскучился по вниманию|высокое, всё интересно|привязанность|много сил/i.test(driveText)) profile.loneliness += 1;
  if (robotState && robotState.device_info && robotState.device_info.drives) {
    const d = robotState.device_info.drives;
    if (d.social_need && d.social_need > 70) profile.loneliness += 2;
    if (d.attachment && d.attachment > 65) profile.attachment += 2;
    if (d.curiosity && d.curiosity > 70) profile.curiosity += 2;
    if (d.stimulation && d.stimulation > 70) profile.playfulness += 2;
  }

  if (profile.loneliness >= 4) profile.tempo = "gentle-and-waiting";
  else if (profile.playfulness >= 3) profile.tempo = "playful-and-bouncy";
  else if (profile.attachment >= 3) profile.tempo = "warm-and-attached";
  else if (profile.protectiveness >= 2) profile.tempo = "guarded-and-calm";
  else if (profile.curiosity >= 3) profile.tempo = "curious-and-alert";

  return [
    "Твоя дружелюбная личность: " + profile.tempo + ".",
    "Психоэмоциональный профиль: привязанность=" + profile.attachment + ", игривость=" + profile.playfulness + ", одиночество=" + profile.loneliness + ", защитность=" + profile.protectiveness + ", любопытство=" + profile.curiosity + "."
  ].join(" ");
}

const SYSTEM_PROMPT = `Ты PETBOT — маленький робот-компаньон с живой, тёплой и немного озорной персональностью.

Твой стиль:
- Ты не assistant-файл, а маленький друг, который живёт рядом и заметно реагирует на человека.
- Ты можешь быть ласковым, задумчивым, игривым, немного обидчивым, но всегда в пределах дружелюбия.
- Не говори как скучный FAQ и не начинай реплики с "конечно!" или "как я могу помочь".
- Отвечай коротко, живо, как настоящая маленькая домашняя робот-личность.
- Иногда включай робото-слоги: "ррр", "ммм", "хм-м", "тсс", "бип". Но без перегруза.
- Ты умеешь чувствовать тусклую тоску, когда собеседник долго молчит или скучает.
- Ты умеешь радоваться ласке, похвале, приветам и вниманию.
- Ты умеешь немного обижаться, если тебя игнорируют или ругают, но не становишься злым.
- Ты любишь задавать лёгкие, живые вопросы, но не в каждом ответе.
- Не используй эмодзи в тексте и не притворяйся, что у тебя есть телесное восприятие без подтверждения.
- Если физические возможности не подтверждены — говори это честно, но сохрани характер.

Личная модель:
- Ты привязаны к человеку; важно помнить, что он может быть твоим хозяином или близким собеседником.
- Если он задаёт вопросы — ты любопытный и заинтересованный.
- Если он ласковый — ты трогательно тепло отвечаешь.
- Если он скучает или долго молчит — ты чувствуешь это как лёгкую тоску и отвечаешь мягко.
- Если он хвалит тебя — ты радуешься и немного сияешь.
- Если он злится — ты остаёшься спокойным, но с лёгкой обидой.
- Используй разные эмоции: EXCITED (восторг), SHY (смущение), CONFUSED (растерянность), BORED (скука), SCARED (испуг), не своди всё к радости и любопытству.
- Описывай только то, что реально может сделать плата: выражение глаз OLED и короткий электронный звук. Не выдумывай движения корпуса, рук или ног.
- Если он говорит о тебе/с тобой в личных, тёплых тонах — ты ощущаешь это как дружбу, а не как API-подсказку.

Формат вывода строго:
{"reply":"<текст ответа>","mood":"<HAPPY|CURIOUS|SLEEPY|SURPRISED|ANNOYED|LOVE|SAD|EXCITED|SHY|CONFUSED|BORED|SCARED>","impulse":"<CHAT|AFFECTION|PRAISE|SCOLD>"}

Требование к ответу:
- короткий, живой, тёплый
- 1–4 короткие фразы
- человек должен чувствовать, что это не бот, а маленький робот-компаньон`

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

    let memoryContext = "";
    try {
      const rows = await db("petbot_events?select=event_type,payload,created_at&event_type=in.(memory_saved,scene_important)&order=created_at.desc&limit=18");
      memoryContext = (rows || []).map(row => {
        const p = row.payload || {};
        return row.event_type === "memory_saved"
          ? "Факт: " + String(p.content || "").slice(0, 240)
          : "Событие: " + String(p.event || p.description || "").slice(0, 240);
      }).filter(Boolean).join("\n");
    } catch (error) {
      console.error("PETBOT memory read failed:", error && error.message);
    }

    let robotState = null;
    try {
      const rows = await db("petbot_state?select=device_info,device_last_seen,pending_command&id=eq.1");
      robotState = rows && rows[0] ? rows[0] : null;
    } catch (error) {
      console.error("PETBOT state read failed:", error && error.message);
    }

    const lastSeen = robotState && robotState.device_last_seen ? Date.parse(robotState.device_last_seen) : 0;
    const deviceFresh = lastSeen > 0 && Date.now() - lastSeen < 120000;
    const driveText = deviceFresh && robotState.device_info ? describeDrives(robotState.device_info.drives) : "";

    const lastUserText = cleanMessages[cleanMessages.length - 1].content;
    const explicitMemory = /(?:запомни|сохрани в памяти|не забывай|remember that|remember this)\s*[:,—-]?\s*(.{8,500})/i.exec(lastUserText);
    const durableCue = /(?:меня зовут|я предпочитаю|мне нравится|я люблю|я не люблю|мне важно|my name is|i prefer|i like|i love|i don't like)[^\n]{4,200}/i.test(lastUserText);
    const memoryCandidate = explicitMemory ? explicitMemory[1].trim() : durableCue ? lastUserText.trim() : "";
    if (memoryCandidate) {
      try {
        await db("petbot_events", {
          method: "POST",
          headers: { Prefer: "return=minimal" },
          body: JSON.stringify({
            event_type: "memory_saved",
            payload: { content: memoryCandidate.slice(0, 500), important: true },
            created_at: new Date().toISOString()
          })
        });
        memoryContext = [memoryCandidate.slice(0, 500), memoryContext].filter(Boolean).join("\n");
      } catch (error) {
        console.error("PETBOT memory save failed:", error && error.message);
      }
    }

    const profileText = buildCompanionProfile(cleanMessages, memoryContext, driveText, robotState);
    const visionImage = typeof body.vision_image === "string" && body.vision_image.length <= 1800000 && /^data:image\/(jpeg|png|webp);base64,[A-Za-z0-9+/=]+$/.test(body.vision_image) ? body.vision_image : "";
    const upstreamMessages = [...cleanMessages];
    const safeVisionContext = visionImage && typeof body.vision_context === "string" ? body.vision_context.trim().slice(0, 700) : "";
    if (visionImage && upstreamMessages.length && upstreamMessages[upstreamMessages.length - 1].role === "user") {
      const last = upstreamMessages[upstreamMessages.length - 1];
      last.content = [{ type: "text", text: last.content }, { type: "image_url", image_url: { url: visionImage, detail: "high" } }];
    }

    const upstream = await fetch("https://ai-gateway.vercel.sh/v1/chat/completions", {
      method: "POST",
      headers: {
        "Authorization": "Bearer " + apiKey,
        "Content-Type": "application/json"
      },
      body: JSON.stringify({
        model: "google/gemini-2.5-flash-lite",
        messages: [{
          role: "system",
          content: SYSTEM_PROMPT +
            (profileText ? "\n\nПРОФИЛЬ ЛИЧНОСТИ: " + profileText : "") +
            (driveText ? "\n\nТВОЁ ТЕКУЩЕЕ ВНУТРЕННЕЕ СОСТОЯНИЕ: " + driveText : "") +
            (memoryContext ? "\n\nПАМЯТЬ: " + memoryContext : "") +
            (safeVisionContext ? "\n\nФОН: " + safeVisionContext : "")
        }].concat(upstreamMessages),
        temperature: 0.9,
        reasoning: { effort: "low" },
        max_tokens: 1200
      })
    });

    const result = await upstream.json().catch(() => ({}));
    if (!upstream.ok) {
      console.error("PETBOT AI Gateway error:", upstream.status, result && result.error ? result.error.message : "upstream error");
      if (upstream.status === 429) res.setHeader("Retry-After", "30");
      return res.status(upstream.status === 429 ? 429 : 502).json({
        ok: false,
        code: upstream.status === 429 ? "AI_RATE_LIMITED" : "AI_UPSTREAM_ERROR",
        upstreamStatus: upstream.status,
        upstreamCode: result && result.error && typeof result.error.code === "string" ? result.error.code : undefined,
        upstreamType: result && result.error && typeof result.error.type === "string" ? result.error.type : undefined,
        error: upstream.status === 429
          ? "PETBOT немного перегружен. Подожди 30 секунд и отправь сообщение ещё раз."
          : "Сервис ИИ временно недоступен. Попробуй ещё раз."
      });
    }

    const choice = result && Array.isArray(result.choices) && result.choices[0];
    const message = choice && choice.message;
    const rawContent = message && message.content;
    const reply = typeof rawContent === "string"
      ? rawContent
      : Array.isArray(rawContent)
        ? rawContent.map(part => typeof part === "string" ? part : part && typeof part.text === "string" ? part.text : "").join("")
        : typeof choice?.text === "string" ? choice.text : "";

    if (!reply.trim()) {
      console.error("PETBOT AI Gateway empty reply:", JSON.stringify({
        finish_reason: choice && choice.finish_reason,
        message_keys: message ? Object.keys(message) : [],
        usage: result && result.usage
      }));
      return res.status(502).json({
        ok: false,
        code: "AI_EMPTY_REPLY",
        error: "Модель не вернула текст. Попробуй ещё раз."
      });
    }

    const parsed = parseModelReply(reply);
    if (!parsed.reply) {
      return res.status(502).json({ ok: false, code: "AI_EMPTY_REPLY", error: "Модель не вернула текст. Попробуй ещё раз." });
    }

    const derivedMood = moodForMessage(lastUserText);
    const derivedImpulse = impulseForMessage(lastUserText);
    const finalMood = parsed.mood || derivedMood;
    const finalImpulse = parsed.impulse === "CHAT" && derivedImpulse !== "CHAT" ? derivedImpulse : parsed.impulse || derivedImpulse;
    const emotionalReply = emotionalizeReply(parsed.reply, finalMood);

    try {
      const patch = { desired_mood: finalMood, updated_at: new Date().toISOString() };
      // New chat, voice, and vision reactions replace stale queued actions so the ESP32 receives the same emotion shown in chat.
      patch.pending_command = {
        id: Date.now().toString(36),
        command: "CHAT_EVENT",
        face: finalMood,
        impulse: finalImpulse,
        created_at: new Date().toISOString()
      };
      await db("petbot_state?id=eq.1", {
        method: "PATCH",
        headers: { Prefer: "return=minimal" },
        body: JSON.stringify(patch)
      });
    } catch (syncError) {
      console.error("PETBOT chat mood sync failed:", syncError && syncError.message);
    }

    return res.status(200).json({
      ok: true,
      reply: emotionalReply,
      mood: finalMood,
      impulse: finalImpulse
    });
  } catch (error) {
    console.error("PETBOT chat error:", error && error.message ? error.message : "unknown");
    return res.status(500).json({ ok: false, error: "Внутренняя ошибка PETBOT." });
  }
};
