import { execSync } from 'child_process';
import esbuild from 'esbuild';
import fs from 'fs-extra';

const outputPath = 'dist';

if (fs.existsSync(outputPath)) fs.rmSync(outputPath, { recursive: true });

esbuild.buildSync({
  entryPoints: ['src/index.ts'],
  outdir: `${outputPath}/es/`,
  format: 'esm',
  bundle: true,
  platform: 'node',
  tsconfig: './tsconfig.json',
  target: 'ESNext',
  external: ['../build/Release/h264.node'],
  outExtension: {
    '.js': '.mjs',
  },
});

esbuild.buildSync({
  entryPoints: ['src/index.ts'],
  outdir: `${outputPath}/lib/`,
  format: 'cjs',
  bundle: true,
  platform: 'node',
  tsconfig: './tsconfig.json',
  target: 'ESNext',
  external: ['../build/Release/h264.node'],
  outExtension: {
    '.js': '.cjs',
  },
});

try {
  execSync(`tsc --emitDeclarationOnly --declaration --project tsconfig.json --outDir ${outputPath}/types_tmp`);
} catch (error) {
  console.error(error.stdout.toString());
}
fs.ensureDirSync(`${outputPath}/types`);
fs.copySync(`${outputPath}/types_tmp`, `${outputPath}/types`);
// fs.copySync(`${outputPath}/types/src`, `${outputPath}/lib`);
fs.rmSync(`${outputPath}/types_tmp`, { recursive: true });

if (fs.existsSync('build/Release/h264_encoder.node')) {
  fs.copySync(`build/Release/h264_encoder.node`, `${outputPath}/build/Release/h264_encoder.node`);
}
