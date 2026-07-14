import {
  CircleMarker,
  MapContainer,
  Polyline,
  Popup,
  TileLayer,
  useMap,
} from "react-leaflet"
import type { LatLngBoundsExpression, LatLngExpression } from "leaflet"
import { useEffect, useMemo, useState } from "react"

import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import type { TelemetryRow } from "@/types/telemetry"
import { fmt } from "@/lib/format"

function validCoordinate(lat?: number, lon?: number) {
  return (
    typeof lat === "number" &&
    typeof lon === "number" &&
    Number.isFinite(lat) &&
    Number.isFinite(lon) &&
    Math.abs(lat) <= 90 &&
    Math.abs(lon) <= 180
  )
}

function distanceMeters(a: [number, number], b: [number, number]) {
  const radius = 6371000
  const lat1 = (a[0] * Math.PI) / 180
  const lat2 = (b[0] * Math.PI) / 180
  const dLat = ((b[0] - a[0]) * Math.PI) / 180
  const dLon = ((b[1] - a[1]) * Math.PI) / 180

  const h =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2

  return 2 * radius * Math.atan2(Math.sqrt(h), Math.sqrt(1 - h))
}

function destinationPoint(
  latDeg: number,
  lonDeg: number,
  bearingDeg: number,
  distanceM: number,
): [number, number] {
  const radius = 6371000
  const bearing = (bearingDeg * Math.PI) / 180
  const lat1 = (latDeg * Math.PI) / 180
  const lon1 = (lonDeg * Math.PI) / 180
  const angularDistance = distanceM / radius

  const lat2 = Math.asin(
    Math.sin(lat1) * Math.cos(angularDistance) +
      Math.cos(lat1) * Math.sin(angularDistance) * Math.cos(bearing),
  )

  const lon2 =
    lon1 +
    Math.atan2(
      Math.sin(bearing) * Math.sin(angularDistance) * Math.cos(lat1),
      Math.cos(angularDistance) - Math.sin(lat1) * Math.sin(lat2),
    )

  return [(lat2 * 180) / Math.PI, (lon2 * 180) / Math.PI]
}

function formatPacketTime(row: TelemetryRow | null) {
  if (!row?.pc_receive_time_iso) return "—"
  return new Date(row.pc_receive_time_iso).toLocaleTimeString()
}

function formatGnssUtc(row: TelemetryRow | null) {
  if (!row?.utc_timestamp) return "—"

  const timestamp = Number(row.utc_timestamp)

  if (!Number.isFinite(timestamp) || timestamp <= 0) return "—"

  return new Date(timestamp * 1000).toLocaleTimeString()
}

function MapController({
  center,
  bounds,
  followLatest,
  fitRequest,
}: {
  center: LatLngExpression
  bounds: LatLngBoundsExpression | null
  followLatest: boolean
  fitRequest: number
}) {
  const map = useMap()

  useEffect(() => {
    if (followLatest) {
      map.setView(center, map.getZoom(), { animate: true })
    }
  }, [center, followLatest, map])

  useEffect(() => {
    if (bounds) {
      map.fitBounds(bounds, { padding: [40, 40] })
    }
  }, [bounds, fitRequest, map])

  return null
}

function PositionHistoryTable({ rows }: { rows: TelemetryRow[] }) {
  const displayRows = [...rows].reverse()

  if (displayRows.length === 0) {
    return (
      <div className="flex h-full items-center justify-center text-sm text-muted-foreground">
        No valid GNSS positions yet.
      </div>
    )
  }

  return (
    <div className="h-full overflow-auto rounded-md border">
      <Table className="min-w-max text-xs">
        <TableHeader className="sticky top-0 z-20 bg-background shadow-sm">
          <TableRow>
            <TableHead className="whitespace-nowrap">PC Time</TableHead>
            <TableHead className="whitespace-nowrap">GNSS UTC</TableHead>
            <TableHead className="whitespace-nowrap">Seq</TableHead>
            <TableHead className="whitespace-nowrap">Lat</TableHead>
            <TableHead className="whitespace-nowrap">Lon</TableHead>
            <TableHead className="whitespace-nowrap">GNSS Alt</TableHead>
            <TableHead className="whitespace-nowrap">Baro Alt</TableHead>
            <TableHead className="whitespace-nowrap">Speed</TableHead>
            <TableHead className="whitespace-nowrap">V Speed</TableHead>
            <TableHead className="whitespace-nowrap">Course</TableHead>
            <TableHead className="whitespace-nowrap">Sats</TableHead>
            <TableHead className="whitespace-nowrap">Fix</TableHead>
          </TableRow>
        </TableHeader>

        <TableBody>
          {displayRows.map((row, index) => (
            <TableRow key={`${row.sequence_number}-${index}`}>
              <TableCell className="whitespace-nowrap">
                {formatPacketTime(row)}
              </TableCell>
              <TableCell className="whitespace-nowrap">
                {formatGnssUtc(row)}
              </TableCell>
              <TableCell>{fmt(row.sequence_number)}</TableCell>
              <TableCell>{fmt(row.latitude_deg, "", 7)}</TableCell>
              <TableCell>{fmt(row.longitude_deg, "", 7)}</TableCell>
              <TableCell>{fmt(row.gnss_altitude_m, " m", 1)}</TableCell>
              <TableCell>{fmt(row.baro_altitude_m, " m", 1)}</TableCell>
              <TableCell>{fmt(row.ground_speed_ms, " m/s", 2)}</TableCell>
              <TableCell>{fmt(row.vertical_speed_ms, " m/s", 2)}</TableCell>
              <TableCell>{fmt(row.course_deg, "°", 2)}</TableCell>
              <TableCell>{fmt(row.gnss_satellites_used)}</TableCell>
              <TableCell>{fmt(row.gnss_fix_type)}</TableCell>
            </TableRow>
          ))}
        </TableBody>
      </Table>
    </div>
  )
}

export function PositionPanel({
  latest,
  history,
}: {
  latest: TelemetryRow | null
  history: TelemetryRow[]
}) {
  const [followLatest, setFollowLatest] = useState(true)
  const [fitRequest, setFitRequest] = useState(0)

  const validRows = history.filter((row) =>
    validCoordinate(row.latitude_deg, row.longitude_deg),
  )

  const trajectory = validRows.map(
    (row) =>
      [row.latitude_deg as number, row.longitude_deg as number] as [
        number,
        number,
      ],
  )

  const launchPoint = trajectory[0] ?? null

  const latestPoint =
    latest && validCoordinate(latest.latitude_deg, latest.longitude_deg)
      ? ([latest.latitude_deg as number, latest.longitude_deg as number] as [
          number,
          number,
        ])
      : null

  const maxAltitudeRow = useMemo(() => {
    const validAltitudeRows = validRows.filter(
      (row) => typeof row.gnss_altitude_m === "number",
    )

    if (validAltitudeRows.length === 0) return null

    return validAltitudeRows.reduce((best, row) =>
      Number(row.gnss_altitude_m) > Number(best.gnss_altitude_m) ? row : best,
    )
  }, [validRows])

  const maxAltitudePoint =
    maxAltitudeRow &&
    validCoordinate(maxAltitudeRow.latitude_deg, maxAltitudeRow.longitude_deg)
      ? ([
          maxAltitudeRow.latitude_deg as number,
          maxAltitudeRow.longitude_deg as number,
        ] as [number, number])
      : null

  const courseLine =
    latestPoint &&
    typeof latest?.course_deg === "number" &&
    Number.isFinite(latest.course_deg)
      ? [
          latestPoint,
          destinationPoint(
            latestPoint[0],
            latestPoint[1],
            latest.course_deg,
            200,
          ),
        ]
      : null

  const center: LatLngExpression = latestPoint ?? launchPoint ?? [48.1351, 11.582]

  const bounds: LatLngBoundsExpression | null =
    trajectory.length >= 2 ? (trajectory as LatLngBoundsExpression) : null

  const distanceFromLaunch =
    launchPoint && latestPoint ? distanceMeters(launchPoint, latestPoint) : null

  return (
    <div className="grid h-full min-h-0 gap-3 xl:grid-cols-[500px_minmax(0,1fr)]">
      <div className="grid min-h-0 grid-rows-[minmax(0,1fr)_155px] gap-3">
        <Card className="min-h-0 overflow-hidden">
          <CardHeader className="px-3 py-1.5">
            <CardTitle className="text-xs">GNSS Position History</CardTitle>
          </CardHeader>
          <CardContent className="h-[calc(100%-32px)] px-3 pb-3">
            <PositionHistoryTable rows={validRows} />
          </CardContent>
        </Card>

        <Card className="overflow-hidden">
          <CardHeader className="px-3 py-1.5">
            <CardTitle className="text-xs">Trajectory Metrics</CardTitle>
          </CardHeader>

          <CardContent className="h-[calc(100%-32px)] overflow-auto px-3 pb-2 text-[11px]">
            <div className="grid grid-cols-2 gap-x-3 gap-y-1">
              <div className="font-medium">Distance from launch</div>
              <div>
                {distanceFromLaunch === null
                  ? "—"
                  : `${distanceFromLaunch.toFixed(1)} m`}
              </div>

              <div className="font-medium">GNSS altitude</div>
              <div>{fmt(latest?.gnss_altitude_m, " m", 1)}</div>

              <div className="font-medium">Baro altitude</div>
              <div>{fmt(latest?.baro_altitude_m, " m", 1)}</div>

              <div className="font-medium">Max altitude</div>
              <div>{fmt(maxAltitudeRow?.gnss_altitude_m, " m", 1)}</div>

              <div className="font-medium">Ground speed</div>
              <div>{fmt(latest?.ground_speed_ms, " m/s", 2)}</div>

              <div className="font-medium">Vertical speed</div>
              <div>{fmt(latest?.vertical_speed_ms, " m/s", 2)}</div>

              <div className="font-medium">Course</div>
              <div>{fmt(latest?.course_deg, "°", 2)}</div>

              <div className="font-medium">Trajectory points</div>
              <div>{trajectory.length}</div>
            </div>
          </CardContent>
        </Card>
      </div>

      <Card className="min-h-0 overflow-hidden">
        <CardHeader className="flex flex-row items-center justify-between px-3 py-1.5">
          <CardTitle className="text-xs">Trajectory / Course Map</CardTitle>

          <div className="flex gap-2">
            <Button
              variant="outline"
              size="sm"
              className="h-7 px-2 text-xs"
              onClick={() => setFitRequest((x) => x + 1)}
            >
              Auto-fit
            </Button>

            <Button
              variant={followLatest ? "default" : "outline"}
              size="sm"
              className="h-7 px-2 text-xs"
              onClick={() => setFollowLatest((x) => !x)}
            >
              {followLatest ? "Following" : "Follow"}
            </Button>
          </div>
        </CardHeader>

        <CardContent className="h-[calc(100%-32px)] px-3 pb-3">
          <div className="map-dashboard h-full overflow-hidden rounded-lg border bg-muted">
            <MapContainer
              center={center}
              zoom={15}
              scrollWheelZoom
              className="h-full w-full"
            >
              <MapController
                center={center}
                bounds={bounds}
                followLatest={followLatest}
                fitRequest={fitRequest}
              />

              <TileLayer
                attribution="&copy; OpenStreetMap contributors &copy; CARTO"
                url="https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png"
              />

              {trajectory.length >= 2 ? (
                <Polyline
                  positions={trajectory}
                  pathOptions={{ color: "#2563eb", weight: 4 }}
                />
              ) : null}

              {courseLine ? (
                <Polyline
                  positions={courseLine}
                  pathOptions={{ color: "#dc2626", weight: 4 }}
                />
              ) : null}

              {launchPoint ? (
                <CircleMarker
                  center={launchPoint}
                  radius={7}
                  pathOptions={{ color: "#16a34a", fillColor: "#16a34a", fillOpacity: 0.8 }}
                >
                  <Popup>Launch point</Popup>
                </CircleMarker>
              ) : null}

              {maxAltitudePoint ? (
                <CircleMarker
                  center={maxAltitudePoint}
                  radius={7}
                  pathOptions={{ color: "#7c3aed", fillColor: "#7c3aed", fillOpacity: 0.8 }}
                >
                  <Popup>
                    Maximum altitude
                    <br />
                    {fmt(maxAltitudeRow?.gnss_altitude_m, " m", 1)}
                  </Popup>
                </CircleMarker>
              ) : null}

              {latestPoint ? (
                <CircleMarker
                  center={latestPoint}
                  radius={8}
                  pathOptions={{ color: "#dc2626", fillColor: "#dc2626", fillOpacity: 0.9 }}
                >
                  <Popup>
                    Latest position
                    <br />
                    Seq {latest?.sequence_number ?? "—"}
                    <br />
                    Course {fmt(latest?.course_deg, "°", 2)}
                    <br />
                    {formatPacketTime(latest)}
                  </Popup>
                </CircleMarker>
              ) : null}
            </MapContainer>
          </div>
        </CardContent>
      </Card>
    </div>
  )
}