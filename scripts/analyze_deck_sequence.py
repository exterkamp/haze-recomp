"""Align a carrier-deck capture sequence and measure paint interior variation.

Requires numpy and opencv-python-headless in the local analysis environment.
This measures one selected deck region, not all rendering correctness. Inspect the
alignment montage and correlation scores before interpreting the statistics.
"""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np


def analyze(manifest, output, reference=None):
    data = json.loads(Path(manifest).read_text())
    images = [cv2.imread(p) for p in data['files']]
    if len(images) < 2 or any(i is None for i in images):
        raise ValueError('Sequence has missing or unreadable captures')
    # Opening carrier camera: restrict registration to the deck plane, away
    # from moving NPCs, sky, rails and the cockpit frame.
    x, y, width, height = 250, 560, 780, 145
    images = [i[y:y+height,x:x+width] for i in images]
    ref = cv2.imread(str(reference))[y:y+height,x:x+width] if reference else images[0]
    gray = cv2.cvtColor(ref, cv2.COLOR_BGR2GRAY).astype(np.float32) / 255
    hsv = cv2.cvtColor(ref, cv2.COLOR_BGR2HSV)
    paint = cv2.inRange(hsv, np.array([12,65,40]), np.array([42,255,255]))
    paint = cv2.erode(paint, np.ones((9,9),np.uint8)) > 0
    if paint.sum() < 100:
        raise ValueError('Selected deck region does not contain enough yellow paint')
    border = np.zeros(gray.shape,np.uint8)
    border[12:-12,12:-12] = 255
    # Fit only unpainted deck. A flickering paint patch must not determine
    # its own alignment. Keep a generous margin around its edges.
    exclusion = cv2.dilate(paint.astype(np.uint8),np.ones((25,25),np.uint8))
    mask = border.copy()
    mask[exclusion > 0] = 0
    aligned, scores = [], []
    for image in images:
        current = cv2.cvtColor(image,cv2.COLOR_BGR2GRAY).astype(np.float32)/255
        transform = np.eye(3,dtype=np.float32)
        score, transform = cv2.findTransformECC(gray,current,transform,
            cv2.MOTION_HOMOGRAPHY,(cv2.TERM_CRITERIA_COUNT|cv2.TERM_CRITERIA_EPS,100,1e-6),mask,5)
        warped = cv2.warpPerspective(image,transform,(width,height),
            flags=cv2.INTER_LINEAR|cv2.WARP_INVERSE_MAP)
        aligned.append(warped.astype(np.float32))
        scores.append(float(score))
    stack = np.stack(aligned)
    valid = (border > 0)
    control = valid & (exclusion == 0)
    # Remove exposure shifts shared by nearby unpainted deck, retaining
    # paint-specific color and visibility changes.
    corrected = stack.copy()
    for i in range(len(stack)):
        corrected[i] -= np.median(stack[i][control]-stack[0][control],axis=0)
    deviation = corrected.std(axis=0).mean(axis=2)
    steps = np.abs(np.diff(corrected,axis=0)).mean(axis=3)
    visibility = []
    for image in aligned:
        hsv_frame = cv2.cvtColor(np.uint8(image),cv2.COLOR_BGR2HSV)
        yellow = cv2.inRange(hsv_frame,np.array([12,65,40]),np.array([42,255,255])) > 0
        visibility.append(float(yellow[paint].mean()))
    heat = cv2.applyColorMap(np.uint8(np.clip(deviation*12,0,255)),cv2.COLORMAP_TURBO)
    montage = np.concatenate([ref,np.uint8(stack[len(stack)//2]),
                              np.uint8(stack[-1]),heat],axis=0)
    output = Path(output)
    output.parent.mkdir(parents=True,exist_ok=True)
    cv2.imwrite(str(output.with_suffix('.png')),montage)
    result = dict(label=data['label'],frames=len(stack),paint_pixels=int(paint.sum()),
        registration_min=min(scores),registration_median=float(np.median(scores)),
        paint_mean_temporal_std=float(deviation[paint].mean()),
        control_mean_temporal_std=float(deviation[control].mean()),
        paint_step_p95=float(np.percentile(steps[:,paint],95)),
        paint_fraction_steps_over_10=float((steps[:,paint]>10).mean()),
        paint_visibility_min=min(visibility),paint_visibility_mean=float(np.mean(visibility)),
        note='8-bit channel units; inspect alignment. Camera/lighting residuals remain. Selected deck region only.')
    output.with_suffix('.json').write_text(json.dumps(result,indent=2))
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('manifest',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--reference',type=Path,help='Use the same reference frame/paint mask for an A/B pair')
    a=p.parse_args()
    print(json.dumps(analyze(a.manifest,a.output,a.reference),indent=2))
