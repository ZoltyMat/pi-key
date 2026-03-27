use anyhow::Result;
use log::{debug, info, warn};
use rand::{Rng, RngExt};
use std::sync::Arc;
use tokio::sync::watch;
use tokio::time::{sleep, Duration};

use crate::config::JigglerConfig;
use crate::transport::{self, HidTransport};

/// Async mouse jiggler loop.
pub async fn run<T: HidTransport>(
    transport: Arc<T>,
    cfg: JigglerConfig,
    mut stop: watch::Receiver<bool>,
) -> Result<()> {
    if !cfg.enabled {
        info!("Jiggler disabled in config");
        return Ok(());
    }

    info!("Jiggler started");

    loop {
        // Random interval between jiggles
        let interval = rand::rng().random_range(cfg.interval_min..=cfg.interval_max);
        debug!("Next jiggle in {:.1}s", interval);

        // Interruptible sleep in 1-second chunks
        let deadline = tokio::time::Instant::now() + Duration::from_secs_f64(interval);
        loop {
            tokio::select! {
                _ = sleep(Duration::from_secs(1).min(deadline - tokio::time::Instant::now())) => {
                    if tokio::time::Instant::now() >= deadline {
                        break;
                    }
                }
                _ = stop.changed() => {
                    if *stop.borrow() {
                        info!("Jiggler stopped");
                        return Ok(());
                    }
                }
            }
        }

        // Check stop again before jiggling
        if *stop.borrow() {
            info!("Jiggler stopped");
            return Ok(());
        }

        if let Err(e) = jiggle_once(transport.as_ref(), &cfg).await {
            warn!("Jiggle failed: {}", e);
        }
    }
}

async fn jiggle_once<T: HidTransport>(transport: &T, cfg: &JigglerConfig) -> Result<()> {
    // Compute all random values upfront (ThreadRng is !Send, can't hold across await)
    let (dx, dy, pause_ms, ret_x, ret_y) = {
        let mut rng = rand::rng();

        let d = if rng.random::<f64>() < cfg.big_move_chance {
            rng.random_range(10..=20)
        } else {
            cfg.max_delta
        };

        let mut dx: i32 = rng.random_range(-d..=d);
        let mut dy: i32 = rng.random_range(-d..=d);
        if dx == 0 && dy == 0 {
            dx = 1;
        }

        let pause_ms = rng.random_range(80..=200u64);
        let ret_x = -dx + rng.random_range(-1..=1);
        let ret_y = -dy + rng.random_range(-1..=1);

        (dx, dy, pause_ms, ret_x, ret_y)
    };

    debug!("Jiggle: dx={} dy={}", dx, dy);

    // Move
    transport::send_mouse(
        transport,
        dx.clamp(-127, 127) as i8,
        dy.clamp(-127, 127) as i8,
        0,
        0,
    )
    .await?;

    // Brief pause
    sleep(Duration::from_millis(pause_ms)).await;

    // Return approximately to origin
    transport::send_mouse(
        transport,
        ret_x.clamp(-127, 127) as i8,
        ret_y.clamp(-127, 127) as i8,
        0,
        0,
    )
    .await?;

    Ok(())
}
