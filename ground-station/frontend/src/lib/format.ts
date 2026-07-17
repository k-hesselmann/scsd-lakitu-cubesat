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

export function packetAgeSeconds(
  pcReceiveTimeUnix?: number | null,
  nowMs = Date.now(),
) {
  if (!pcReceiveTimeUnix) return null
  return Math.max(0, nowMs / 1000 - pcReceiveTimeUnix)
}
