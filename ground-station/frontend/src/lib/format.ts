export function fmt(value: unknown, suffix = "", decimals?: number) {
  if (value === null || value === undefined) return "—"

  if (typeof value === "number" && decimals !== undefined) {
    return `${value.toFixed(decimals)}${suffix}`
  }

  if (typeof value === "boolean") {
    return value ? "YES" : "NO"
  }

  return `${value}${suffix}`
}

export function formatGnssUtc(secondsOfDay?: number | null) {
  if (secondsOfDay === null || secondsOfDay === undefined) return "\u2014"
  if (!Number.isInteger(secondsOfDay) || secondsOfDay < 0 || secondsOfDay >= 86400) return "\u2014"

  const hours = Math.floor(secondsOfDay / 3600)
  const minutes = Math.floor((secondsOfDay % 3600) / 60)
  const seconds = secondsOfDay % 60
  return [hours, minutes, seconds].map((value) => value.toString().padStart(2, "0")).join(":") + " UTC"
}

export function packetAgeSeconds(
  pcReceiveTimeUnix?: number | null,
  nowMs = Date.now(),
) {
  if (!pcReceiveTimeUnix) return null
  return Math.max(0, nowMs / 1000 - pcReceiveTimeUnix)
}
