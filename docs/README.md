# C64 Ultimate OBS Plugin - GitHub Pages Site

This directory contains the GitHub Pages website for the C64 Ultimate OBS Plugin.

## 🌐 Live Site

Visit the live website at: [chrisgleissner.github.io/c64u-obs](https://chrisgleissner.github.io/c64u-obs)

## 📁 Structure

- `index.html` - Main landing page with modern design and feature highlights
- `images/` - Website images (copied from `../doc/img/`)
- `_config.yml` - Jekyll/GitHub Pages configuration

## 🚀 Deployment

The site is automatically deployed to GitHub Pages when changes are pushed to the main branch.

To enable GitHub Pages for this repository:
1. Go to repository Settings → Pages
2. Set Source to "Deploy from a branch"
3. Select branch: `main`
4. Select folder: `/docs`
5. Click Save

## 🛠 Local Development

To test the site locally:

```bash
# If you have Jekyll installed
cd docs
jekyll serve

# Or use GitHub Pages gem
bundle exec jekyll serve
```

The site will be available at `http://localhost:4000/c64u-obs`

## 📝 Content Updates

The website content is automatically pulled from:
- Main project README.md for feature descriptions
- Release information from GitHub releases
- Screenshots from `doc/img/` directory

When updating the main project documentation, consider updating the website content as well to keep it in sync.