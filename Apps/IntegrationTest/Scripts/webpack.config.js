const path = require("path");
const webpack = require("webpack");

module.exports = {
  entry: "./src/index.ts",
  output: {
    filename: "BabylonInterop.bundle.js",
    path: path.resolve(__dirname, "dist"),
  },
  resolve: {
    extensions: [".ts", ".js"],
    // Force a single @babylonjs/core instance even when @babylonjs/loaders
    // is resolved through a symlinked file: dependency. Without this, webpack
    // can pick up two copies of core (the linked one and a transitive one),
    // breaking instanceof checks and module-level singletons.
    alias: {
      "@babylonjs/core": path.resolve(
        __dirname,
        "../../../../Babylon.js/packages/public/@babylonjs/core"
      ),
      "@babylonjs/loaders": path.resolve(
        __dirname,
        "../../../../Babylon.js/packages/public/@babylonjs/loaders"
      ),
    },
    symlinks: false,
  },
  module: {
    rules: [
      {
        test: /\.ts$/,
        use: "ts-loader",
        exclude: /node_modules/,
      },
      // The local Babylon.js packages (linked via file:) ship a
      // package.json `sideEffects` array that uses Windows-style backslash
      // separators. Webpack's sideEffects glob matching uses POSIX
      // separators, so none of those patterns match and webpack
      // tree-shakes the side-effect wrapper modules (e.g.
      // Engines/nativeEngine.js -> RegisterNativeEngine()), leaving the
      // mixin un-applied and `engine.getCaps()` undefined at runtime.
      // Force webpack to treat every file in those packages as having
      // side effects to neutralize the broken globs.
      {
        test: /[\\/]Babylon\.js[\\/]packages[\\/]public[\\/](@babylonjs|glTF2Interface)[\\/]/,
        sideEffects: true,
      },
    ],
  },
  plugins: [
    new webpack.optimize.LimitChunkCountPlugin({
      maxChunks: 1,
    }),
  ],
  devtool: "source-map",
};
