use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use tauri::AppHandle;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum SalmonRunRank {
    Apprentice,
    PartTimer,
    GoGetter,
    Overachiever,
    Profreshional,
    ProfreshionalPlus0,
    ProfreshionalPlus1,
    ProfreshionalPlus2,
    ProfreshionalPlus3,
    EggsecutiveVp,
}

impl Default for SalmonRunRank {
    fn default() -> Self { Self::PartTimer }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SalmonRunProfile {
    pub rank: SalmonRunRank,
    pub rank_points: u32,
    pub total_golden_eggs: u64,
    pub shifts_cleared: u32,
    pub shifts_failed: u32,
    pub grizzco_points: u64,
}

impl Default for SalmonRunProfile {
    fn default() -> Self {
        Self {
            rank: SalmonRunRank::default(),
            rank_points: 0,
            total_golden_eggs: 0,
            shifts_cleared: 0,
            shifts_failed: 0,
            grizzco_points: 0,
        }
    }
}

fn profile_path(app: &AppHandle) -> PathBuf {
    app.path().app_data_dir().expect("no app data dir").join("salmon_run_profile.json")
}

#[tauri::command]
pub async fn get_salmon_run_rank(app: AppHandle) -> Result<SalmonRunRank, String> {
    let path = profile_path(&app);
    if !path.exists() { return Ok(SalmonRunRank::default()); }
    let raw = std::fs::read_to_string(&path).map_err(|e| format!("read error: {e}"))?;
    let profile: SalmonRunProfile = serde_json::from_str(&raw).map_err(|e| format!("parse error: {e}"))?;
    Ok(profile.rank)
}

#[tauri::command]
pub async fn get_salmon_run_profile(app: AppHandle) -> Result<SalmonRunProfile, String> {
    let path = profile_path(&app);
    if !path.exists() { return Ok(SalmonRunProfile::default()); }
    let raw = std::fs::read_to_string(&path).map_err(|e| format!("read error: {e}"))?;
    serde_json::from_str(&raw).map_err(|e| format!("parse error: {e}"))
}

#[tauri::command]
pub async fn apply_shift_result(
    app: AppHandle,
    waves_cleared: u8,
    golden_eggs: u32,
    grizzco_points: u32,
    hazard_level: u32,
) -> Result<SalmonRunProfile, String> {
    let path = profile_path(&app);
    let mut profile = if path.exists() {
        let raw = std::fs::read_to_string(&path).map_err(|e| format!("read error: {e}"))?;
        serde_json::from_str::<SalmonRunProfile>(&raw).map_err(|e| format!("parse error: {e}"))?
    } else {
        SalmonRunProfile::default()
    };

    profile.total_golden_eggs += u64::from(golden_eggs);
    profile.grizzco_points += u64::from(grizzco_points);

    if waves_cleared == 3 {
        profile.shifts_cleared += 1;
        profile.rank_points = profile.rank_points.saturating_add(hazard_level / 10);
        profile = check_promotion(profile);
    } else {
        profile.shifts_failed += 1;
        let loss = (3u8.saturating_sub(waves_cleared)) as u32 * 20;
        profile.rank_points = profile.rank_points.saturating_sub(loss);
        profile = check_demotion(profile);
    }

    let serialized = serde_json::to_string_pretty(&profile).map_err(|e| format!("serialize error: {e}"))?;
    std::fs::write(&path, serialized).map_err(|e| format!("write error: {e}"))?;
    Ok(profile)
}

fn check_promotion(mut p: SalmonRunProfile) -> SalmonRunProfile {
    if p.rank == SalmonRunRank::EggsecutiveVp {
        p.rank_points = p.rank_points.min(999);
        return p;
    }
    if p.rank_points >= 100 {
        p.rank = match p.rank {
            SalmonRunRank::Apprentice         => SalmonRunRank::PartTimer,
            SalmonRunRank::PartTimer          => SalmonRunRank::GoGetter,
            SalmonRunRank::GoGetter           => SalmonRunRank::Overachiever,
            SalmonRunRank::Overachiever       => SalmonRunRank::Profreshional,
            SalmonRunRank::Profreshional      => SalmonRunRank::ProfreshionalPlus0,
            SalmonRunRank::ProfreshionalPlus0 => SalmonRunRank::ProfreshionalPlus1,
            SalmonRunRank::ProfreshionalPlus1 => SalmonRunRank::ProfreshionalPlus2,
            SalmonRunRank::ProfreshionalPlus2 => SalmonRunRank::ProfreshionalPlus3,
            SalmonRunRank::ProfreshionalPlus3 => SalmonRunRank::EggsecutiveVp,
            SalmonRunRank::EggsecutiveVp      => SalmonRunRank::EggsecutiveVp,
        };
        p.rank_points -= 100;
    }
    p
}

fn check_demotion(mut p: SalmonRunProfile) -> SalmonRunProfile {
    if p.rank == SalmonRunRank::Apprentice { p.rank_points = 0; }
    p
}
