import type { LatLngBoundsExpression, LatLngExpression } from "leaflet"
import { useEffect, useMemo, useState } from "react"
import { CircleMarker, MapContainer, Polyline, Popup, TileLayer, useMap } from "react-leaflet"

import { ExpandablePanelCard } from "@/components/ExpandablePanelCard"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table"
import { fmt, formatGnssUtc } from "@/lib/format"
import { gnssFixIsValid } from "@/lib/telemetryHealth"
import { sampleEvenly } from "@/lib/telemetrySeries"
import type { TelemetryRow } from "@/types/telemetry"

function validCoordinate(lat?: number, lon?: number) {
  return typeof lat === "number"
    && typeof lon === "number"
    && Number.isFinite(lat)
    && Number.isFinite(lon)
    && Math.abs(lat) <= 90
    && Math.abs(lon) <= 180
}

function packetTime(row: TelemetryRow) {
  return row.pc_receive_time_iso
    ? new Date(row.pc_receive_time_iso).toLocaleTimeString()
    : "\u2014"
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
    if (followLatest) map.setView(center, map.getZoom(), { animate: true })
  }, [center, followLatest, map])

  useEffect(() => {
    if (bounds) map.fitBounds(bounds, { padding: [40, 40] })
  }, [bounds, fitRequest, map])

  return null
}

export function PositionPanel({ latest, history }: { latest: TelemetryRow | null; history: TelemetryRow[] }) {
  const [followLatest, setFollowLatest] = useState(true)
  const [fitRequest, setFitRequest] = useState(0)
  const validRows = useMemo(
    () => history.filter((row) => gnssFixIsValid(row) && validCoordinate(row.latitude_deg, row.longitude_deg)),
    [history],
  )
  const trajectoryRows = useMemo(() => sampleEvenly(validRows, 250), [validRows])
  const trajectory = useMemo(
    () => trajectoryRows.map((row) => [row.latitude_deg as number, row.longitude_deg as number] as [number, number]),
    [trajectoryRows],
  )
  const tableRows = useMemo(() => [...validRows].reverse().slice(0, 75), [validRows])
  const launchPoint = trajectory[0] ?? null
  const latestFixValid = gnssFixIsValid(latest)
  const latestPoint = latest && latestFixValid && validCoordinate(latest.latitude_deg, latest.longitude_deg)
    ? [latest.latitude_deg as number, latest.longitude_deg as number] as [number, number]
    : null
  const maxAltitudeRow = useMemo(
    () => validRows.reduce<TelemetryRow | null>(
      (best, row) => !best || (row.gnss_altitude_m ?? -Infinity) > (best.gnss_altitude_m ?? -Infinity) ? row : best,
      null,
    ),
    [validRows],
  )
  const maxAltitudePoint = maxAltitudeRow && validCoordinate(maxAltitudeRow.latitude_deg, maxAltitudeRow.longitude_deg)
    ? [maxAltitudeRow.latitude_deg as number, maxAltitudeRow.longitude_deg as number] as [number, number]
    : null
  const center: LatLngExpression = latestPoint ?? launchPoint ?? [48.1351, 11.582]
  const bounds: LatLngBoundsExpression | null = trajectory.length >= 2
    ? trajectory as LatLngBoundsExpression
    : null

  const positionHistory = (
    <div className="h-full overflow-auto rounded-md border">
      <Table className="min-w-max text-xs">
        <TableHeader className="sticky top-0 z-10 bg-background">
          <TableRow>
            <TableHead>Ground time</TableHead>
            <TableHead>Seq</TableHead>
            <TableHead>Latitude</TableHead>
            <TableHead>Longitude</TableHead>
            <TableHead>GNSS alt MSL</TableHead>
            <TableHead>Baro alt relative</TableHead>
            <TableHead>Ground speed</TableHead>
            <TableHead>Vertical speed</TableHead>
          </TableRow>
        </TableHeader>
        <TableBody>
          {tableRows.map((row, index) => (
            <TableRow key={`${row.sequence_number}-${index}`}>
              <TableCell>{packetTime(row)}</TableCell>
              <TableCell>{fmt(row.sequence_number)}</TableCell>
              <TableCell>{fmt(row.latitude_deg, "", 7)}</TableCell>
              <TableCell>{fmt(row.longitude_deg, "", 7)}</TableCell>
              <TableCell>{fmt(row.gnss_altitude_m, " m", 1)}</TableCell>
              <TableCell>{fmt(row.baro_altitude_m, " m", 1)}</TableCell>
              <TableCell>{fmt(row.ground_speed_ms, " m/s", 2)}</TableCell>
              <TableCell>{fmt(row.vertical_speed_ms, " m/s", 2)}</TableCell>
            </TableRow>
          ))}
        </TableBody>
      </Table>
    </div>
  )

  const trajectoryMap = (
    <div className="map-dashboard h-full overflow-hidden rounded-lg border bg-muted">
      <MapContainer center={center} zoom={15} scrollWheelZoom className="h-full w-full">
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
          <Polyline positions={trajectory} pathOptions={{ color: "#2563eb", weight: 4 }} />
        ) : null}
        {launchPoint ? (
          <CircleMarker center={launchPoint} radius={7} pathOptions={{ color: "#16a34a", fillColor: "#16a34a", fillOpacity: 0.8 }}>
            <Popup>First valid 3D GNSS point</Popup>
          </CircleMarker>
        ) : null}
        {maxAltitudePoint ? (
          <CircleMarker center={maxAltitudePoint} radius={7} pathOptions={{ color: "#7c3aed", fillColor: "#7c3aed", fillOpacity: 0.8 }}>
            <Popup>Maximum GNSS altitude MSL<br />{fmt(maxAltitudeRow?.gnss_altitude_m, " m", 1)}</Popup>
          </CircleMarker>
        ) : null}
        {latestPoint ? (
          <CircleMarker center={latestPoint} radius={8} pathOptions={{ color: "#dc2626", fillColor: "#dc2626", fillOpacity: 0.9 }}>
            <Popup>
              Latest valid 3D GNSS point<br />
              Seq {fmt(latest?.sequence_number)}<br />
              {packetTime(latest as TelemetryRow)}
            </Popup>
          </CircleMarker>
        ) : null}
      </MapContainer>
    </div>
  )

  const trajectoryActions = (
    <>
      <Button
        variant="outline"
        size="sm"
        className="h-7 px-2 text-xs"
        onClick={() => setFitRequest((value) => value + 1)}
      >
        Auto-fit
      </Button>
      <Button
        variant={followLatest ? "default" : "outline"}
        size="sm"
        className="h-7 px-2 text-xs"
        onClick={() => setFollowLatest((value) => !value)}
      >
        {followLatest ? "Following" : "Follow"}
      </Button>
    </>
  )

  return (
    <div className="grid h-full min-h-0 gap-3 xl:grid-cols-[460px_minmax(0,1fr)]">
      <div className="grid min-h-0 grid-rows-[minmax(0,1fr)_190px] gap-3">
        <ExpandablePanelCard
          title="Valid 3D GNSS Position History"
          className="gap-0 py-0"
          headerClassName="h-9 px-4 py-2"
          contentClassName="flex-1 px-3 pb-3"
          expandedContentClassName="h-full"
        >
          {positionHistory}
        </ExpandablePanelCard>

        <Card>
          <CardHeader className="px-3 py-1.5">
            <CardTitle className="text-xs">Protocol-v8 GNSS Metrics</CardTitle>
          </CardHeader>
          <CardContent className="grid h-[calc(100%-32px)] grid-cols-2 gap-x-3 gap-y-1 overflow-auto px-3 pb-2 text-[11px]">
            <div>GNSS 3D fix valid</div><div>{latest ? latestFixValid ? "YES" : "NO" : "\u2014"}</div>
            <div>UTC</div><div>{latestFixValid ? formatGnssUtc(latest?.gnss_utc_sod) : "\u2014"}</div>
            <div>Fix type</div><div>{fmt(latest?.gnss_fix_type)}</div>
            <div>Satellites</div><div>{fmt(latest?.gnss_satellites_used)}</div>
            <div>Course</div><div>{fmt(latestFixValid ? latest?.course_deg : undefined, "\u00B0", 2)}</div>
            <div>GNSS altitude MSL</div><div>{fmt(latestFixValid ? latest?.gnss_altitude_m : undefined, " m", 1)}</div>
            <div>Vertical speed</div><div>{fmt(latestFixValid ? latest?.vertical_speed_ms : undefined, " m/s", 2)}</div>
            <div>Ground speed</div><div>{fmt(latestFixValid ? latest?.ground_speed_ms : undefined, " m/s", 2)}</div>
            <div>Sample age</div><div>{fmt(latest?.sample_age_ms, " ms")}</div>
            <div>Trajectory points</div><div>{validRows.length}</div>
          </CardContent>
        </Card>
      </div>

      <ExpandablePanelCard
        title="3D GNSS Trajectory"
        actions={trajectoryActions}
        className="gap-0 py-0"
        headerClassName="min-h-9 px-4 py-1"
        contentClassName="flex-1 px-3 pb-3"
        expandedContentClassName="h-full"
      >
        {trajectoryMap}
      </ExpandablePanelCard>
    </div>
  )
}
