import { useEffect, useState } from "react"
import {
  loadAlertThresholds,
  saveAlertThresholds,
  type AlertThresholds,
} from "@/lib/thresholds"

export function useAlertThresholds() {
  const [thresholds, setThresholdsState] = useState<AlertThresholds>(() =>
    loadAlertThresholds(),
  )

  useEffect(() => {
    function reload() {
      setThresholdsState(loadAlertThresholds())
    }

    window.addEventListener("storage", reload)
    window.addEventListener("thresholds-updated", reload)

    return () => {
      window.removeEventListener("storage", reload)
      window.removeEventListener("thresholds-updated", reload)
    }
  }, [])

  function setThresholds(next: AlertThresholds) {
    saveAlertThresholds(next)
    setThresholdsState(next)
  }

  return { thresholds, setThresholds }
}