export default function handler(_request, response) {
  response.setHeader("Cache-Control", "no-store");
  response.status(200).json({
    ok: true,
    service: "petbot-cloud",
    version: "0.1.0",
    robotConnected: false,
    motorControlEnabled: false,
    timestamp: new Date().toISOString()
  });
}
