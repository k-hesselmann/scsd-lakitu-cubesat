export type ChartAxisDomain = [
  (dataMinimum: number) => number,
  (dataMaximum: number) => number,
]

export function adaptiveAxisDomain(
  padding: number,
  lowerBound = Number.NEGATIVE_INFINITY,
  upperBound = Number.POSITIVE_INFINITY,
): ChartAxisDomain {
  return [
    (dataMinimum) => Math.max(lowerBound, dataMinimum - padding),
    (dataMaximum) => Math.min(upperBound, dataMaximum + padding),
  ]
}
