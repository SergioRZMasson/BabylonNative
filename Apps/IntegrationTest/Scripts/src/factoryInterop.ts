import {
  Matrix,
  TmpVectors,
  Vector4,
} from "@babylonjs/core/Maths/math.vector";

export function transformPoint(
  x: number,
  y: number,
  z: number,
  invert: boolean,
  aspectRatio: number,
  orthographic: boolean,
  fovOrOrthographicSize: number,
  minZ: number,
  maxZ: number,
  positionX: number,
  positionY: number,
  positionZ: number,
  targetX: number,
  targetY: number,
  targetZ: number,
  upVectorX: number,
  upVectorY: number,
  upVectorZ: number
): Float32Array {
  const viewMatrix = Matrix.LookAtRHToRef(
    TmpVectors.Vector3[0].set(positionX, positionY, positionZ),
    TmpVectors.Vector3[1].set(targetX, targetY, targetZ),
    TmpVectors.Vector3[2].set(upVectorX, upVectorY, upVectorZ),
    TmpVectors.Matrix[0]
  );

  const projectionMatrix = orthographic
    ? Matrix.OrthoOffCenterLHToRef(
        -fovOrOrthographicSize * aspectRatio,
        fovOrOrthographicSize * aspectRatio,
        -fovOrOrthographicSize,
        fovOrOrthographicSize,
        minZ,
        maxZ,
        TmpVectors.Matrix[1]
      )
    : Matrix.PerspectiveFovRHToRef(
        fovOrOrthographicSize,
        aspectRatio,
        minZ,
        maxZ,
        TmpVectors.Matrix[1]
      );

  const viewProjectionMatrix = viewMatrix.multiplyToRef(
    projectionMatrix,
    TmpVectors.Matrix[2]
  );

  if (invert) {
    viewProjectionMatrix.invert();
  }

  const result = Vector4.TransformCoordinatesFromFloatsToRef(
    x,
    y,
    z,
    viewProjectionMatrix,
    TmpVectors.Vector4[0]
  );

  return new Float32Array([result.x, result.y, result.z, result.w]);
}
