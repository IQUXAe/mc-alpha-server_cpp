use libc::{c_int, size_t};
use std::slice;
use crate::noise::NoiseGeneratorOctaves;

#[no_mangle]
pub unsafe extern "C" fn alpha_density_generate_field(
    out_field: *mut f64,
    out_len: size_t,
    var2: c_int,
    var3: c_int,
    var4: c_int,
    var5: c_int,
    var6: c_int,
    var7: c_int,
    temperatures: *const f64,
    humidities: *const f64,
    field_715_a: *mut NoiseGeneratorOctaves,
    field_714_b: *mut NoiseGeneratorOctaves,
    field_703_m: *mut NoiseGeneratorOctaves,
    field_705_k: *mut NoiseGeneratorOctaves,
    field_704_l: *mut NoiseGeneratorOctaves,
) {
    let field = slice::from_raw_parts_mut(out_field, out_len);
    let temperatures = slice::from_raw_parts(temperatures, 256);
    let humidities = slice::from_raw_parts(humidities, 256);

    let gen_715 = &*field_715_a;
    let gen_714 = &*field_714_b;
    let gen_703 = &*field_703_m;
    let gen_705 = &*field_705_k;
    let gen_704 = &*field_704_l;

    let var8 = 684.412;
    let var10 = 684.412;

    let mut field_4226_g = vec![0.0; (var5 * var7) as usize];
    let mut field_4225_h = vec![0.0; (var5 * var7) as usize];
    let mut field_4229_d = vec![0.0; (var5 * var6 * var7) as usize];
    let mut field_4228_e = vec![0.0; (var5 * var6 * var7) as usize];
    let mut field_4227_f = vec![0.0; (var5 * var6 * var7) as usize];

    gen_715.func_4103_a(&mut field_4226_g, var2, var4, var5 as usize, var7 as usize, 1.121, 1.121);
    gen_714.func_4103_a(&mut field_4225_h, var2, var4, var5 as usize, var7 as usize, 200.0, 200.0);
    gen_703.func_648_a(&mut field_4229_d, var2 as f64, var3 as f64, var4 as f64, var5 as usize, var6 as usize, var7 as usize, var8 / 80.0, var10 / 160.0, var8 / 80.0);
    gen_705.func_648_a(&mut field_4228_e, var2 as f64, var3 as f64, var4 as f64, var5 as usize, var6 as usize, var7 as usize, var8, var10, var8);
    gen_704.func_648_a(&mut field_4227_f, var2 as f64, var3 as f64, var4 as f64, var5 as usize, var6 as usize, var7 as usize, var8, var10, var8);

    let mut var14 = 0;
    let mut var15 = 0;
    let var16 = 16 / var5;

    for var17 in 0..var5 {
        let var18 = var17 * var16 + var16 / 2;
        for var19 in 0..var7 {
            let var20 = var19 * var16 + var16 / 2;
            let var21 = temperatures[(var18 * 16 + var20) as usize];
            let var23 = humidities[(var18 * 16 + var20) as usize] * var21;
            let mut var25 = 1.0 - var23;
            var25 *= var25;
            var25 *= var25;
            var25 = 1.0 - var25;
            let mut var27 = (field_4226_g[var15] + 256.0) / 512.0;
            var27 *= var25;
            if var27 > 1.0 { var27 = 1.0; }

            let mut var29 = field_4225_h[var15] / 8000.0;
            if var29 < 0.0 { var29 = -var29 * 0.3; }

            var29 = var29 * 3.0 - 2.0;
            if var29 < 0.0 {
                var29 /= 2.0;
                if var29 < -1.0 { var29 = -1.0; }
                var29 /= 1.4;
                var29 /= 2.0;
                var27 = 0.0;
            } else {
                if var29 > 1.0 { var29 = 1.0; }
                var29 /= 8.0;
            }

            if var27 < 0.0 { var27 = 0.0; }

            var27 += 0.5;
            var29 = var29 * var6 as f64 / 16.0;
            let var31 = var6 as f64 / 2.0 + var29 * 4.0;
            var15 += 1;

            for var33 in 0..var6 {
                let mut var34;
                let mut var36 = (var33 as f64 - var31) * 12.0 / var27;
                if var36 < 0.0 { var36 *= 4.0; }

                let var38 = field_4228_e[var14] / 512.0;
                let var40 = field_4227_f[var14] / 512.0;
                let var42 = (field_4229_d[var14] / 10.0 + 1.0) / 2.0;

                if var42 < 0.0 {
                    var34 = var38;
                } else if var42 > 1.0 {
                    var34 = var40;
                } else {
                    var34 = var38 + (var40 - var38) * var42;
                }

                var34 -= var36;
                if var33 > var6 - 4 {
                    let var44 = (var33 - (var6 - 4)) as f64 / 3.0;
                    var34 = var34 * (1.0 - var44) + -10.0 * var44;
                }

                field[var14] = var34;
                var14 += 1;
            }
        }
    }
}
