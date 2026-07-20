import type { TelemetryRow, TimelineEvent } from "@/types/telemetry"

export function telemetryRowId(row: TelemetryRow) {
  return `${row.pc_receive_time_unix ?? "unknown"}-${row.sequence_number ?? "radio-error"}`
}

export function mergeTelemetryHistory(
  existing: TelemetryRow[],
  incoming: TelemetryRow[],
  limit: number,
) {
  if (incoming.length === 0) return existing

  const unique = new Map<string, TelemetryRow>()

  for (const row of [...existing, ...incoming]) {
    unique.set(telemetryRowId(row), row)
  }

  return [...unique.values()]
    .sort(
      (a, b) =>
        (a.pc_receive_time_unix ?? 0) - (b.pc_receive_time_unix ?? 0),
    )
    .slice(-limit)
}

export function appendTelemetryHistory(
  existing: TelemetryRow[],
  incoming: TelemetryRow[],
  limit: number,
) {
  if (incoming.length === 0) return existing

  const lastExistingTime = existing.at(-1)?.pc_receive_time_unix ?? 0
  const chronological = incoming.every(
    (row, index) =>
      (row.pc_receive_time_unix ?? 0) >=
      (index === 0
        ? lastExistingTime
        : (incoming[index - 1].pc_receive_time_unix ?? 0)),
  )

  if (!chronological) {
    return mergeTelemetryHistory(existing, incoming, limit)
  }

  const retained = existing.slice(Math.max(0, existing.length - limit))
  const ids = new Set(retained.map(telemetryRowId))

  for (const row of incoming) {
    const id = telemetryRowId(row)
    const lastIndex = retained.length - 1

    if (lastIndex >= 0 && telemetryRowId(retained[lastIndex]) === id) {
      retained[lastIndex] = row
      continue
    }

    if (!ids.has(id)) {
      retained.push(row)
      ids.add(id)
    }
  }

  return retained.slice(-limit)
}

export function prependTimelineEvents(
  existing: TimelineEvent[],
  incoming: TimelineEvent[],
  limit: number,
) {
  if (incoming.length === 0) return existing

  const newestFirst = [...incoming].sort((a, b) => b.time_unix - a.time_unix)
  const seen = new Set<string>()
  const result: TimelineEvent[] = []

  for (const event of [...newestFirst, ...existing]) {
    if (seen.has(event.id)) continue
    seen.add(event.id)
    result.push(event)
    if (result.length === limit) break
  }

  return result
}

export function sampleEvenly<T>(values: T[], maxPoints: number) {
  if (values.length <= maxPoints || maxPoints < 2) return values

  const sampled: T[] = []
  const step = (values.length - 1) / (maxPoints - 1)

  for (let index = 0; index < maxPoints; index += 1) {
    sampled.push(values[Math.round(index * step)])
  }

  return sampled
}
