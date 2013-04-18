/**
 * issuance.c
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Pim Vullers, Radboud University Nijmegen, July 2011.
 */

#include "issuance.h"

#include "APDU.h"
#include "arithmetic.h"
#include "debug.h"
#include "externals.h"
#include "memory.h"
#include "random.h"
#include "sizes.h"
#include "types.h"
#include "types.debug.h"
#include "utils.h"

/********************************************************************/
/* Issuing functions                                                */
/********************************************************************/

/**
 * Construct a commitment (round 1)
 *
 * @param issuerKey (S, R, n)
 * @param proof (nonce, context)
 * @param masterSecret
 * @param number for U
 * @param number for UTilde
 * @param vPrime in signature.v + SIZE_V - SIZE_VPRIME
 * @param vPrimeTilde in vHat
 * @param vPrimeHat in vHat
 * @param mTilde[0] in mHat[0]
 * @param s_A in mHat[0]
 * @param nonce
 * @param buffer for hash of SIZE_BUFFER_C1
 * @param (buffer for SpecialModularExponentiation of SIZE_N)
 */
void constructCommitment(void) {

  // Generate random vPrime
  RandomBits(session.issue.vPrime, LENGTH_VPRIME);
  debugValue("vPrime", session.issue.vPrime, SIZE_VPRIME);

  // Compute U = S^vPrime * R[0]^m[0] mod n
  ModExpSpecial(SIZE_VPRIME, session.issue.vPrime, public.issue.U,
    public.issue.buffer.number[0]);
  debugNumber("U = S^vPrime mod n", public.issue.U);
  ModExpSecure(SIZE_M, SIZE_N, masterSecret, credential->issuerKey.n,
    credential->issuerKey.R[0], public.issue.buffer.number[0]);
  debugNumber("buffer = R[0]^m[0] mod n", public.issue.buffer.number[0]);
  ModMul(SIZE_N, public.issue.U, public.issue.buffer.number[0],
    credential->issuerKey.n);
  debugNumber("U = U * buffer mod n", public.issue.U);

  // Compute P1:
  // - Generate random vPrimeTilde, mTilde[0]
  RandomBits(session.issue.vPrimeHat, LENGTH_VPRIME_);
  debugValue("vPrimeTilde", session.issue.vPrimeHat, SIZE_VPRIME_);
  RandomBits(session.issue.sHat, LENGTH_S_);
  debugValue("sTilde", session.issue.sHat, SIZE_S_);

  // - Compute UTilde = S^vPrimeTilde * R[0]^sTilde mod n
  ModExpSpecial(SIZE_VPRIME_, session.issue.vPrimeHat,
    public.issue.buffer.number[0], public.issue.buffer.number[1]);
  debugNumber("UTilde = S^vPrimeTilde mod n", public.issue.buffer.number[0]);
  ModExp(SIZE_S_, SIZE_N, session.issue.sHat, credential->issuerKey.n,
    credential->issuerKey.R[0], public.issue.buffer.number[1]);
  debugNumber("buffer = R[0]^sTilde mod n", public.issue.buffer.number[1]);
  ModMul(SIZE_N, public.issue.buffer.number[0],
    public.issue.buffer.number[1], credential->issuerKey.n);
  debugNumber("UTilde = UTilde * buffer mod n", public.issue.buffer.number[0]);

  // - Compute challenge c = H(context | U | UTilde | nonce)
  public.issue.list[0].data = credential->proof.context;
  public.issue.list[0].size = SIZE_H;
  public.issue.list[1].data = public.issue.U;
  public.issue.list[1].size = SIZE_N;
  public.issue.list[2].data = public.issue.buffer.number[0];
  public.issue.list[2].size = SIZE_N;
  public.issue.list[3].data = public.issue.nonce;
  public.issue.list[3].size = SIZE_STATZK;
  ComputeHash(public.issue.list, 4, session.issue.challenge,
    public.issue.buffer.data, SIZE_BUFFER_C1);
  debugHash("c", session.issue.challenge);

  // - Compute response vPrimeHat = vPrimeTilde + c * vPrime
  crypto_compute_vPrimeHat();
  debugValue("vPrimeHat", session.issue.vPrimeHat, SIZE_VPRIME_);

  // - Compute response sHat = sTilde + c * s
  crypto_compute_sHat();
  debugValue("sHat", session.issue.sHat, SIZE_S_);

  // Generate random n_2
  RandomBits(credential->proof.nonce, LENGTH_STATZK);
  debugNonce("nonce", credential->proof.nonce);
}

/**
 * Construct the signature (round 3, part 1)
 *
 *   A, e, v = v' + v''
 *
 * @param v' in session.issue.vPrime of size SIZE_VPRIME
 * @param v'' in public.apdu.data of size SIZE_V
 * @param signature (A, e, v) in credential->signature
 */
void constructSignature(void) {

  // Compute v = v' + v'' using add with carry
  debugValue("v'", session.issue.vPrime, SIZE_VPRIME);
  debugValue("v''", public.apdu.data, SIZE_V);
  __push(credential->signature.v + SIZE_V/2);
  __push(BLOCKCAST(1 + SIZE_V/2)(session.issue.vPrime + SIZE_VPRIME - SIZE_V/2 - 1));
  __push(BLOCKCAST(1 + SIZE_V/2)(public.apdu.data + SIZE_V/2));
  __code(ADDN, 1 + SIZE_V/2);
  __code(POPN, 1 + SIZE_V/2);
  __code(STOREI, 1 + SIZE_V/2);

  CarryFlag(flag);
  if (flag != 0x00) {
    debugMessage("Addition with carry, adding 1");
    __code(INCN, public.apdu.data, SIZE_V/2);
  }

  // First push some zero's to compensate for the size difference
  __push(credential->signature.v);
  __code(PUSHZ, SIZE_V - SIZE_VPRIME);
  __push(BLOCKCAST(SIZE_VPRIME - SIZE_V/2 - 1)(session.issue.vPrime));
  __push(BLOCKCAST(SIZE_V/2)(public.apdu.data));
  __code(ADDN, SIZE_V/2);
  __code(POPN, SIZE_V/2);
  __code(STOREI, SIZE_V/2);
  debugValue("v = v' + v''", credential->signature.v, SIZE_V);
}

/**
 * (OPTIONAL) Verify the signature (round 3, part 2)
 *
 *   Z =?= A^e * S^v * R where R = R[i]^m[i] forall i
 *
 * @param signature (A, e, v) in credential->signature
 * @param issuerKey (Z, S, R, n) in credential->issuerKey
 * @param attributes (m[0]...m[l]) in credential->attribute
 * @param masterSecret
 */
void verifySignature(void) {
  Byte i;

  // Clear the memory before starting computations
  ClearBytes(sizeof(CLSignatureVerification), session.base);

  // Compute Z' = S^v mod n
  ModExpSpecial(SIZE_V, credential->signature.v, session.vfySig.ZPrime, session.vfySig.buffer);
  debugNumber("Z' = S^v mod n", session.vfySig.buffer);

  // Compute Z' = S^v * A^e mod n
  ModExp(SIZE_E, SIZE_N, credential->signature.e, credential->issuerKey.n, credential->signature.A, session.vfySig.buffer);
  debugNumber("buffer = A^e mod n", session.vfySig.buffer);
  ModMul(SIZE_N, session.vfySig.ZPrime, session.vfySig.buffer, credential->issuerKey.n);
  debugNumber("Z' = Z' * buffer mod n", session.vfySig.ZPrime);

  // Compute Z' = S^v * A^e * R[i]^m[i] mod n forall i
  ModExpSecure(SIZE_M, SIZE_N, masterSecret, credential->issuerKey.n, credential->issuerKey.R[0], session.vfySig.buffer);
  debugNumber("buffer = R[0]^ms mod n", session.vfySig.buffer);
  ModMul(SIZE_N, session.vfySig.ZPrime, session.vfySig.buffer, credential->issuerKey.n);
  debugNumber("Z' = Z' * buffer mod n", session.vfySig.ZPrime);
  for (i = 0; i < credential->size; i++) {
    ModExp(SIZE_M, SIZE_N, credential->attribute[i], credential->issuerKey.n, credential->issuerKey.R[i + 1], session.vfySig.buffer);
    debugNumber("buffer = R[i]^m[i] mod n", session.vfySig.buffer);
    ModMul(SIZE_N, session.vfySig.ZPrime, session.vfySig.buffer, credential->issuerKey.n);
    debugNumber("Z' = Z' * buffer mod n", session.vfySig.ZPrime);
  }

  // Verify Z =?= Z'
  if (Compare(SIZE_N, credential->issuerKey.Z, session.vfySig.ZPrime) != 0) {
    // TODO: clear already stored things?
    debugError("verifySignature(): verification of signature failed");
    APDU_ReturnSW(SW_CONDITIONS_NOT_SATISFIED);
  }
}

/**
 * (OPTIONAL) Verify the proof (round 3, part 3)
 *
 *   c =?= H(context, A^e, A, nonce, A^(c + s_e * e))
 *
 * @param signature (A, e) in credential->signature
 * @param issuerKey (n) in credential->issuerKey
 * @param proof (nonce, context, challenge, response) in credential->proof
 */
void verifyProof(void) {

  // Clear the memory before starting computations
  ClearBytes(sizeof(IssuanceProofVerification), public.base);
  ClearBytes(sizeof(IssuanceProofSession), session.base);

  // Compute Q = A^e mod n
  ModExp(SIZE_E, SIZE_N, credential->signature.e, credential->issuerKey.n, credential->signature.A, session.vfyPrf.Q);
  debugNumber("Q = A^e mod n", session.vfyPrf.Q);

  // Compute AHat = A^(c + s_e * e) = Q^s_e * A^c mod n
  ModExp(SIZE_N, SIZE_N, credential->proof.response, credential->issuerKey.n, session.vfyPrf.Q, public.vfyPrf.buffer);
  debugNumber("buffer = Q^s_e mod n", public.vfyPrf.buffer);
  ModExp(SIZE_H, SIZE_N, credential->proof.challenge, credential->issuerKey.n, credential->signature.A, session.vfyPrf.AHat);
  debugNumber("AHat = A^c mod n", session.vfyPrf.AHat);
  ModMul(SIZE_N, session.vfyPrf.AHat, public.vfyPrf.buffer, credential->issuerKey.n);
  debugNumber("AHat = AHat * buffer", session.vfyPrf.AHat);

  // Compute challenge c' = H(context | Q | A | nonce | AHat)
  session.vfyPrf.list[0].data = credential->proof.context;
  session.vfyPrf.list[0].size = SIZE_H;
  session.vfyPrf.list[1].data = session.vfyPrf.Q;
  session.vfyPrf.list[1].size = SIZE_N;
  session.vfyPrf.list[2].data = credential->signature.A;
  session.vfyPrf.list[2].size = SIZE_N;
  session.vfyPrf.list[3].data = credential->proof.nonce;
  session.vfyPrf.list[3].size = SIZE_STATZK;
  session.vfyPrf.list[4].data = session.vfyPrf.AHat;
  session.vfyPrf.list[4].size = SIZE_N;
  ComputeHash(session.vfyPrf.list, 5, session.vfyPrf.challenge, public.vfyPrf.buffer, SIZE_BUFFER_C2);
  debugHash("c'", session.vfyPrf.challenge);

  // Verify c =?= c'
  if (Compare(SIZE_H, credential->proof.challenge, session.vfyPrf.challenge) != 0) {
    // TODO: clear already stored things?
    debugError("verifyProof(): verification of P2 failed");
    APDU_ReturnSW(SW_CONDITIONS_NOT_SATISFIED);
  }
}
